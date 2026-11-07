#ifndef GPU_AUTORES_H
#define GPU_AUTORES_H

/* Display-only policy. Samples contain GPU milliseconds summed over `frames`
 * game vblanks, including idle vblanks between submissions. */
typedef struct {
    int ceiling, limit, scale, constrained;
    unsigned cooldown, count, over, headroom;
    double total, mean;
} AutoResolution;

void autoResolutionReset(AutoResolution* a, int ceiling);
void autoResolutionRestart(AutoResolution* a); /* fresh samples, retain the cap */
void autoResolutionCeiling(AutoResolution* a, int ceiling);
void autoResolutionSample(AutoResolution* a, int scale, double budget, double ms, unsigned frames);

#endif
