#ifndef PC_SPEEDUP_H
#define PC_SPEEDUP_H
/* Battle's VSync(0): optional deadline-based pacing after a host overrun.
 * Original timing remains available with Speedup off. */
void pcBattleVSync(void);
/* Field's main flip uses the same optional catch-up. The field still
 * enforces its authored minimum frame interval after drawing. */
void pcFieldVSync(void);
#endif
