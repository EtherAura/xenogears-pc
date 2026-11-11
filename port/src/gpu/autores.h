#ifndef GPU_AUTORES_H
#define GPU_AUTORES_H

/* Display-only policy. Samples contain GPU milliseconds summed over `frames`
 * game vblanks, including idle vblanks between submissions. The budget is
 * per video frame; at a game speed of `factor` (battle speed) that many game
 * vblanks share one, so each gets budget / factor. */
typedef struct {
    int ceiling, limit, scale, constrained;
    int factor; /* the effective game speed of the samples (1 at reset) */
    unsigned cooldown, count, over, headroom;
    double total, mean;
} AutoResolution;

void autoResolutionReset(AutoResolution* a, int ceiling);
void autoResolutionRestart(AutoResolution* a); /* fresh samples, retain the cap */
void autoResolutionCeiling(AutoResolution* a, int ceiling);
/* The effective game speed from now on (values below 1 count as 1). A change
 * restarts the samples: those still in flight were drawn at the old speed. */
void autoResolutionSpeed(AutoResolution* a, int factor);
void autoResolutionSample(AutoResolution* a, int scale, double budget, double ms, unsigned frames);

#endif
