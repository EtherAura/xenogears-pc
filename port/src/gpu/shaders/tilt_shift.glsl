// The tilt-shift (xg-rgcf.5.14, 5.15), for dof.comp's blur and light.comp's
// particles: what the display shows blurs toward each of its edges, from this
// share of its height in from them, faintly at first and most at the edge,
// around a sharp area whose corners are rounded, the radius this share of the
// height (from in-game captures: strips 5% of the height at
// the top and bottom, then the sides too, the corners rounded, and the fade
// eased in from three strips' depth).
// (testing: the blur a step softer and reaching less far in: 0.15 to
// 0.10, then 0.07; then, at 9x, a third as strong (scene.c tiltRadius) over
// 0.12, so it fades in gradually rather than smearing a thin strip)
const float TILT_REACH = 0.12;
const float TILT_CORNER = 0.10;

// The share of the tilt-shift's radius at a point (PS1 pixels from the top
// left of the shown picture, `shown` its size): 0 in the sharp area, growing
// with the square of the distance from it to 1 at the edges (sooner in the
// corners, and beyond, in rows the display does not show).
float tiltRamp(vec2 at, vec2 shown) {
    float reach = max(TILT_REACH * shown.y, 1.0), corner = TILT_CORNER * shown.y;
    // The distance from the sharp area: the frame inset by `reach`, its
    // corners rounded (a rounded box's distance; negative inside it).
    vec2 q = abs(at - 0.5 * shown) - (0.5 * shown - reach - corner);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - corner;
    float t = clamp(d / reach, 0.0, 1.0);
    return t * t;
}
