/*
 * Helper threads for work the game thread splits into equal parts, run at
 * the same time on every thread, the caller's included: the 60 Hz
 * Battling's page compares (port/src/game/battling/between.c). Modelled on
 * the rewind lanes (rewind.c), but a pool any caller can use.
 */
#ifndef XG_WORKERS_H
#define XG_WORKERS_H

#define WORKERS_MAX 8     /* parts at most, the caller's included */
#define WORKERS_HELPERS 2 /* helper threads by default (XG_WORKERS=<n> for a run; 0: the caller alone) */

/* fn(arg, part, parts) on each of the parts threads at once (part 0 on the
 * caller's), back when all are done; the parts (1 with no helper). */
int workersRun(void (*fn)(void* arg, int part, int parts), void* arg);
/* The helpers stopped and joined (quit.c, before SDL_Quit; also at exit). */
void workersShutdown(void);

#endif
