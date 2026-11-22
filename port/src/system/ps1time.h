#ifndef PORT_SYSTEM_PS1TIME_H
#define PORT_SYSTEM_PS1TIME_H

/*
 * The PS1's time for the game's work (xg-wv9t): the port does a frame's work
 * at once, where the PS1's CPU and GPU took their time, so heavy scenes drew
 * fewer pictures there (an idle Gear battle: one every third vblank). Work
 * counters say how much a stretch of the game's work was: the GTE commands it
 * ran (gte.c) and the primitives the GPU drew (gp0.c, native packets only).
 * ps1time.c turns them into PS1 time, and logs them (XG_PS1TIME_LOG) for the
 * model's fit against Beetle PSX logs (tools/ps1time_fit.py).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* gte.c: commands by number (the command word's low six bits), counted
 * while gteCountOn. */
extern uint32_t gteCommandCount[64];
extern int gteCountOn;

/* gp0.c: the native primitives drawn while ps1timeGpuOn, by kind: their count
 * and their pixels inside the drawing area (polygons and rectangles), pixels
 * (lines), or the rectangle's (fills, VRAM copies and loads). */
enum {
    PT_POLY_FLAT,
    PT_POLY_GOURAUD,
    PT_POLY_TEX4,
    PT_POLY_TEX8,
    PT_POLY_TEX15,
    PT_RECT_FLAT,
    PT_RECT_TEX4,
    PT_RECT_TEX8,
    PT_RECT_TEX15,
    PT_LINE,
    PT_FILL,
    PT_COPY,
    PT_LOAD,
    PT_KINDS
};
typedef struct {
    double count[PT_KINDS];
    double pixels[PT_KINDS];
    double semiPixels;     /* of those, semi-transparent (read and blend) */
    double shadedTexPixels; /* textured and Gouraud-shaded or modulated */
} Ps1timeGpu;
extern Ps1timeGpu ps1timeGpu;
extern int ps1timeGpuOn;
/* gp0.c: a VRAM transfer LIBGPU does itself (LoadImage, MoveImage, ClearImage). */
void ps1timeGpuRect(int kind, int w, int h);

/* ps1time.c: the events the work falls between. */
void ps1timeVSync(int32_t arg);      /* VSync called (after the replay lined it up) */
void ps1timeVSyncDone(int32_t arg);  /* ... and returns */
void ps1timeDrawSync(void);          /* DrawSync called */
void ps1timeTimeRead(void);          /* a root counter read (GetRCnt) */
void ps1timeMode(int mode);          /* bootGame starts a game mode */
/* The PS1 frame rate (setting ps1_frame_rate): whether it is chosen, and choose it (saves). */
int ps1timeFrameRateOn(void);
void ps1timeSetFrameRate(int enabled);
void ps1timeGpuQueued(char what);    /* a GPU job queued: 'O' DrawOTag, 'P' DrawPrim, 'L' LoadImage,
                                      * 'M' MoveImage, 'C' ClearImage, 'S' StoreImage */

#ifdef __cplusplus
}
#endif

#endif
