#ifndef PORT_SPEEDUP_H
#define PORT_SPEEDUP_H

/* Speedup: the port's single enhancement switch, off by default (the
 * original timing). On, new memory-card accesses complete immediately,
 * ordinary disc loads run at 8x, battle and field pacing use a vblank that
 * already passed during rendering instead of adding a fresh wait, and
 * Smooth motion (gpu/interpolation.c: fields, the world map) is on by default: host-rate
 * presentation, 60 Hz unless chosen otherwise, which adds one frame of
 * latency. Queued events retain their deadlines; gameplay/audio clocks and
 * streamed reads stay unchanged. Off bypasses smoothing whatever its stored
 * preference and keeps the original waits, frame cadence and latency.
 *
 * speedupEnabled() is read on hot paths (every battle/field frame, each CD
 * sector of a load, each card access): it keeps the value and reads the
 * setting again only after a settings change (settingsRevision: the menu,
 * speedupSetEnabled, a netplay pin). XG_SPEEDUP is read at the first use;
 * speedupRefresh() reads it again (a test that changes it while running). */
int speedupEnabled(void);
void speedupSetEnabled(int enabled);
void speedupRefresh(void);

#endif
