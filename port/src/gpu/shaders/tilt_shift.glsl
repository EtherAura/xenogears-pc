// The tilt-shift's strips (xg-rgcf.5.14), for dof.comp's blur and light.comp's
// particles: a strip along the top and one along the bottom of what the
// display shows, each this share of its height (carl: the blur on 10% of the
// frame in all).
const float TILT_STRIP = 0.05;

// The share of the tilt-shift's radius at a row (from the top of the shown
// rows, `shown` of them): 0 outside the strips, rising smoothly to 1 at the
// edges (and beyond the bottom one, in rows the display does not show).
float tiltRamp(float row, float shown) {
    float t = clamp(1.0 - min(row, shown - row) / max(TILT_STRIP * shown, 1.0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
