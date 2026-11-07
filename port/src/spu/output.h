#ifndef PORT_SPU_OUTPUT_H
#define PORT_SPU_OUTPUT_H

/* The sound output's controls for the settings menu (port/src/spu/output.c). */

/* The settings menu paused the game (1) or resumed it (0): the sound fades
 * out, and back in with the first frames after. */
void spuOutputPause(int on);
int spuOutputDryPushes(void); /* pushes that found the device's queue empty */
int spuOutputCuts(void);      /* lossy cuts so far (a queue over the limit dropped, an arrival trimmed) */
int spuOutputStarved(void);   /* device requests the queue could not fill so far (underruns) */
/* The queue now, for the settings menu's meter (port/src/ui/diagnostics.cpp),
 * in frames at 44.1 kHz: what the stream holds, its smoothed level, the
 * target (audio_latency plus what underruns grew it by), the base and the
 * ceiling (audio_latency_max). All 0 before the output opens. */
typedef struct {
    int open, queued, target, base, max, prefilling;
    double average;
} SpuOutputLevels;
void spuOutputLevels(SpuOutputLevels* out);

/* Take audio_latency and volume from the settings again (output.c). */
void spuOutputApplySettings(void);

#endif /* PORT_SPU_OUTPUT_H */
