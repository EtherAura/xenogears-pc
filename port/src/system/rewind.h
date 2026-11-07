#ifndef PORT_SYSTEM_REWIND_H
#define PORT_SYSTEM_REWIND_H

/*
 * Rewind (rewind.c): the last seconds of play kept as reverse deltas of
 * save-state captures; hold R (F10 when R plays a PS1 button) or Select+L2 on
 * a gamepad to go back. Off by default (settings.ini rewind=1, F1 > Save
 * states).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int on, seconds, interval, budget, lanes; /* the settings (budget in MB; lanes: the compare's threads) */
    double kept;                       /* seconds the history holds now */
    size_t bytes;                      /* memory it takes */
    double captureMs, worstMs;         /* a capture's cost on the game thread: the mean, the worst */
} RewindStats;

int rewindEnabled(void);
void rewindShutdown(void); /* join the packing worker before SDL_Quit */
void rewindStats(RewindStats* out);
void rewindSetOn(int on);
void rewindSetSeconds(int seconds);
void rewindSetInterval(int vblanks);
void rewindSetBudget(int megabytes);
void rewindSetLanes(int lanes); /* 1..8, the game thread included */

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_REWIND_H */
