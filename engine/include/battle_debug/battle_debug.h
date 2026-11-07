#ifndef BATTLE_DEBUG_BATTLE_DEBUG_H
#define BATTLE_DEBUG_BATTLE_DEBUG_H

/* Battle debugger overlay (disc 12/06, dev kit only), loaded at 0x80280000. */

#include "common.h"
#include "psyq.h"
#include "main/task.h"

/* CPU/GPU load meter task (createDebugLoadMeter): a draw + update task pair
 * (allocateTaskPair) followed by the meter state. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_cpu;         /* displayed CPU time (m40 >> 0) */
    /* 0x3C */ s32 m3C_gpu;         /* displayed GPU time */
    /* 0x40 */ s32 m40_cpuSmooth;   /* smoothed battleDrawTimming0 * 16 */
    /* 0x44 */ s32 m44_gpuSmooth;   /* smoothed battleDrawTimming1 * 16 */
    /* 0x48 */ s32 m48_cpuPeak;
    /* 0x4C */ s32 m4C_gpuPeak;
    /* 0x50 */ s32 m50_cpuPeakTimer; /* frames the peak is held (80) */
    /* 0x54 */ s32 m54_gpuPeakTimer;
} BattleDebugMeter; /* 0x58 */

#endif /* BATTLE_DEBUG_BATTLE_DEBUG_H */
