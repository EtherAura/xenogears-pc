// The present pass's filters (present.frag includes this; present.c picks
// one, present_filter=): how the picture is resampled to the window.
//
// The including shader provides vec3 filterPixel(ivec2 s): the picture's
// pixel s, 0..255 per channel, s inside [0, size); and uint filterWord(int
// i): word i of the buffer xbr's codes are kept in. Each filter takes f, the
// target pixel inside the destination rectangle (0 .. dest - 1), the
// picture's size and the destination's size, and returns 0..255 per channel.
// Our own code, from the published descriptions of the algorithms.
//
//   nearest  each target pixel the picture pixel under its centre; exact in
//            integers, so a whole-number scale gives every pixel the same
//            block (the default)
//   linear   the four nearest pixels blended (bilinear)
//   sharp    area: each target pixel the average of the picture under it.
//            Whole multiples are exactly nearest; at other scales only the
//            target pixels that straddle two picture pixels blend, so pixels
//            stay crisp and even; a smaller target averages what it covers
//   bicubic  Catmull-Rom over 4 x 4 pixels, kept inside the range of the
//            nearest 2 x 2 (no ringing): smooth, for movies
//   xbr      edges in pixel art: corners cut along the edges xBR's rules find
//            (below); as sharp where the target is no larger than the picture

#define FILTER_NEAREST 0
#define FILTER_LINEAR 1
#define FILTER_SHARP 2
#define FILTER_BICUBIC 3
#define FILTER_XBR 4

vec3 filterNearest(ivec2 f, ivec2 size, ivec2 dest) {
    return filterPixel(((2 * f + 1) * size) / (2 * dest));
}

vec3 filterLinear(ivec2 f, ivec2 size, ivec2 dest) {
    vec2 s = (vec2(f) + 0.5) * vec2(size) / vec2(dest) - 0.5;
    ivec2 s0 = ivec2(floor(s));
    vec2 t = s - vec2(s0);
    ivec2 last = size - 1;
    ivec2 a = clamp(s0, ivec2(0), last);
    ivec2 b = clamp(s0 + 1, ivec2(0), last);
    vec3 top = mix(filterPixel(a), filterPixel(ivec2(b.x, a.y)), t.x);
    vec3 bottom = mix(filterPixel(ivec2(a.x, b.y)), filterPixel(b), t.x);
    return mix(top, bottom, t.y);
}

// ---- sharp: area ----------------------------------------------------------------------
//
// Measured in 1 / dest of a picture pixel (1 / size of a target pixel) per
// axis, target pixel i spans [i * size, (i + 1) * size) and picture pixel j
// spans [j * dest, (j + 1) * dest): the overlaps are whole numbers, and a
// picture pixel that covers the whole target pixel gets weight 1 exactly.

vec3 filterSharp(ivec2 f, ivec2 size, ivec2 dest) {
    ivec2 lo = f * size;
    ivec2 hi = lo + size;
    ivec2 j0 = lo / dest;
    ivec2 j1 = (hi - 1) / dest;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int y = j0.y; y <= j1.y; y++) {
        float wy = float(min(hi.y, (y + 1) * dest.y) - max(lo.y, y * dest.y)) / float(size.y);
        for (int x = j0.x; x <= j1.x; x++) {
            float w = wy * float(min(hi.x, (x + 1) * dest.x) - max(lo.x, x * dest.x)) / float(size.x);
            sum += w * filterPixel(ivec2(x, y));
            total += w;
        }
    }
    return sum / total;
}

// ---- bicubic ----------------------------------------------------------------------------

// Catmull-Rom's weights for the four pixels around a point t past the second.
vec4 catmullRom(float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                0.5 * t3 - 0.5 * t2);
}

vec3 filterBicubic(ivec2 f, ivec2 size, ivec2 dest) {
    vec2 s = (vec2(f) + 0.5) * vec2(size) / vec2(dest) - 0.5;
    ivec2 s0 = ivec2(floor(s));
    vec2 t = s - vec2(s0);
    vec4 wx = catmullRom(t.x);
    vec4 wy = catmullRom(t.y);
    ivec4 xs = clamp(s0.x + ivec4(-1, 0, 1, 2), 0, size.x - 1);
    ivec4 ys = clamp(s0.y + ivec4(-1, 0, 1, 2), 0, size.y - 1);
    vec3 sum = vec3(0.0);
    vec3 lo = vec3(255.0);
    vec3 hi = vec3(0.0);
    for (int j = 0; j < 4; j++) {
        vec3 line = vec3(0.0);
        for (int i = 0; i < 4; i++) {
            vec3 c = filterPixel(ivec2(xs[i], ys[j]));
            line += wx[i] * c;
            if (i == 1 || i == 2) {
                if (j == 1 || j == 2) {
                    lo = min(lo, c);
                    hi = max(hi, c);
                }
            }
        }
        sum += wy[j] * line;
    }
    return clamp(sum, lo, hi);
}

// The present picture is opaque. Texture edges use the same rules with
// clear-pixel coverage (hires.frag).
vec4 xbrPixel(ivec2 s) { return vec4(filterPixel(s), 255.0); }
#include "edge.glsl"

// codes: the first word of the picture's codes (row after row) through
// filterWord, or -1 to find them here.
vec3 filterXbr(ivec2 f, ivec2 size, ivec2 dest, int codes) {
    vec2 k = vec2(dest) / vec2(size);
    if (min(k.x, k.y) <= 1.0) {
        return filterSharp(f, size, dest);
    }
    /* E and the target pixel's centre inside it, exactly as nearest finds E. */
    ivec2 n = (2 * f + 1) * size;
    ivec2 e = n / (2 * dest);
    vec2 uv = vec2(n - e * 2 * dest) / vec2(2 * dest);
    uint code;
    if (codes >= 0) {
        code = filterWord(codes + e.y * size.x + e.x);
    } else {
        code = xbrLoad(e, size) ? xbrCode() : 0u;
    }
    return code == 0u ? filterPixel(e) : xbrBlend(e, size, code, uv, k).rgb;
}

vec3 filterPicture(int mode, ivec2 f, ivec2 size, ivec2 dest, int codes) {
    switch (mode) {
    case FILTER_LINEAR:
        return filterLinear(f, size, dest);
    case FILTER_SHARP:
        return filterSharp(f, size, dest);
    case FILTER_BICUBIC:
        return filterBicubic(f, size, dest);
    case FILTER_XBR:
        return filterXbr(f, size, dest, codes);
    default:
        return filterNearest(f, size, dest);
    }
}
