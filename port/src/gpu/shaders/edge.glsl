// Shared original xBR rules for picture and texture edges. The caller
// supplies xbrPixel: premultiplied RGB and coverage, all in 0..255.
// Opaque picture pixels have coverage 255; clear texture pixels are zero.

// ---- xbr: edges in pixel art -------------------------------------------------------------
//
// After Hyllian's xBR (level 2), as published: each corner of the picture
// pixel E under the target pixel is checked for an edge passing it. Seen
// from the corner between E, its neighbour F across and H down (I diagonal),
// with the 5 x 5 pixels around E named
//
//          .  B1 C1  .            (B1, C1 and the extended names below
//      A0  A  B  C  C4             are only used where they appear)
//      D0  D  E  F  F4
//      G0  G  H  I  I4
//          .  H5 I5  .
//
// an edge runs along F-H when the colour changes less along that diagonal
// than across it, weighting the pixels next to the corner four times:
//   d(E,C) + d(E,G) + d(I,F4) + d(I,H5) + 4 d(F,H)
//     < d(H,D) + d(H,I5) + d(F,I4) + d(F,B) + 4 d(E,I)
// and E differs from F and H. The corner of a larger shape is kept square
// unless the pixels around it say it is a diagonal. E's corner beyond the
// edge then takes the colour of F or H (whichever is closer to E's): a 45°
// cut through the middles of E's sides there, or a 2:1 cut along the side
// where the edge continues (F like G: shallow; H like C: steep). The cut's
// edge is a ramp one target pixel wide, so it stays sharp at any scale.
// The other three corners are the same seen in a mirror.
//
// Colours compare in luma and chroma; differences of about a 5-bit step
// (the PS1's dither) count as the same colour.
//
// Two halves: xbrCode() finds the cuts of E's corners, once per picture
// pixel (the picture prepass before the present pass, present.frag: its
// codes come through filterWord), and xbrBlend() applies them to a target
// pixel. Without the prepass the present pass finds them itself.

#define XBR_SAME 0.05

// A corner's 4 bits in the code (corner (sx, sy) at bit 4 * ((sx > 0) + 2 * (sy > 0))).
#define XBR_CUT 1u     // a cut (45° unless one of the next)
#define XBR_SHALLOW 2u // 2:1 along the side across (x)
#define XBR_STEEP 4u   // 2:1 along the side down (y)
#define XBR_FROM_H 8u  // the cut takes H's colour, else F's

vec4 xbrYuv[25];

// Neighbour (dx, dy) of E seen from the corner toward (sx, sy).
int xbrAt(int sx, int sy, int dx, int dy) {
    return (sy * dy + 2) * 5 + sx * dx + 2;
}

float xbrDist(int a, int b) {
    vec4 d = abs(xbrYuv[a] - xbrYuv[b]);
    return d.x + 0.25 * (d.y + d.z) + d.w;
}

bool xbrSame(int a, int b) {
    return xbrDist(a, b) < XBR_SAME;
}

vec4 xbrMetric(vec4 c) {
    const mat3 toYuv = mat3(0.299, -0.169, 0.5, 0.587, -0.331, -0.419, 0.114, 0.5, -0.081) / 255.0;
    return vec4(toYuv * c.rgb, c.a / 255.0);
}

// Each corner immediately rejects E equal to either of its two neighbours.
// If both horizontal or both vertical neighbours equal E, all four reject:
// five taps suffice, with exactly the same corner decisions as the full set.
bool xbrLoad(ivec2 e, ivec2 size) {
    ivec2 last = size - 1;
    for (int d = -1; d <= 1; d++) {
        xbrYuv[12 + d] = xbrMetric(xbrPixel(clamp(e + ivec2(d, 0), ivec2(0), last)));
    }
    for (int d = -1; d <= 1; d += 2) {
        xbrYuv[12 + 5 * d] = xbrMetric(xbrPixel(clamp(e + ivec2(0, d), ivec2(0), last)));
    }
    if ((xbrSame(12, 11) && xbrSame(12, 13)) || (xbrSame(12, 7) && xbrSame(12, 17))) {
        return false;
    }
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if ((abs(dx) != 2 || abs(dy) != 2) &&
                !(dy == 0 && abs(dx) <= 1) && !(dx == 0 && abs(dy) <= 1)) {
                vec4 c = xbrPixel(clamp(e + ivec2(dx, dy), ivec2(0), last));
                xbrYuv[(dy + 2) * 5 + dx + 2] = xbrMetric(c);
            }
        }
    }
    return true;
}

// The corner toward (sx, sy): XBR_* bits, 0 when no edge passes it.
uint xbrCorner(int sx, int sy) {
    int E = xbrAt(sx, sy, 0, 0);
    int F = xbrAt(sx, sy, 1, 0);
    int H = xbrAt(sx, sy, 0, 1);
    if (xbrSame(E, F) || xbrSame(E, H)) {
        return 0u;
    }
    int I = xbrAt(sx, sy, 1, 1);
    int B = xbrAt(sx, sy, 0, -1);
    int C = xbrAt(sx, sy, 1, -1);
    int D = xbrAt(sx, sy, -1, 0);
    int G = xbrAt(sx, sy, -1, 1);
    int F4 = xbrAt(sx, sy, 2, 0);
    int I4 = xbrAt(sx, sy, 2, 1);
    int H5 = xbrAt(sx, sy, 0, 2);
    int I5 = xbrAt(sx, sy, 1, 2);
    float along = xbrDist(E, C) + xbrDist(E, G) + xbrDist(I, F4) + xbrDist(I, H5) + 4.0 * xbrDist(F, H);
    float across = xbrDist(H, D) + xbrDist(H, I5) + xbrDist(F, I4) + xbrDist(F, B) + 4.0 * xbrDist(E, I);
    if (!(along < across)) {
        return 0u;
    }
    bool diagonal = (!xbrSame(F, B) && !xbrSame(F, C)) || (!xbrSame(H, D) && !xbrSame(H, G)) ||
                    (xbrSame(E, I) && ((!xbrSame(F, F4) && !xbrSame(F, I4)) || (!xbrSame(H, H5) && !xbrSame(H, I5)))) ||
                    xbrSame(E, G) || xbrSame(E, C);
    if (!diagonal) {
        return 0u;
    }
    uint c = XBR_CUT;
    float fg = xbrDist(F, G);
    float hc = xbrDist(H, C);
    if (2.0 * fg <= hc && !xbrSame(E, G) && !xbrSame(D, G)) {
        c |= XBR_SHALLOW;
    }
    if (fg >= 2.0 * hc && !xbrSame(E, C) && !xbrSame(B, C)) {
        c |= XBR_STEEP;
    }
    if (xbrDist(E, F) > xbrDist(E, H)) {
        c |= XBR_FROM_H;
    }
    return c;
}

// E's four corners (xbrLoad first).
uint xbrCode() {
    return xbrCorner(-1, -1) | xbrCorner(1, -1) << 4 | xbrCorner(-1, 1) << 8 | xbrCorner(1, 1) << 12;
}

// How much of the target pixel at p (picture pixels, the corner at (1, 1))
// lies past the line n . p = c, as a ramp one target pixel wide (k: target
// pixels per picture pixel).
float xbrCover(vec2 p, vec2 n, float c, vec2 k) {
    return clamp((dot(n, p) - c) / (n.x / k.x + n.y / k.y) + 0.5, 0.0, 1.0);
}

// Picture pixel e with its corners cut by the code, at uv inside it.
vec4 xbrBlend(ivec2 e, ivec2 size, uint code, vec2 uv, vec2 k) {
    vec4 col = xbrPixel(e);
    for (int i = 0; i < 4; i++) {
        uint bits = (code >> (4 * i)) & 15u;
        if (bits == 0u) {
            continue;
        }
        int sx = (i & 1) != 0 ? 1 : -1;
        int sy = (i & 2) != 0 ? 1 : -1;
        vec2 p = vec2(sx > 0 ? uv.x : 1.0 - uv.x, sy > 0 ? uv.y : 1.0 - uv.y);
        float a = xbrCover(p, vec2(1.0, 1.0), 1.5, k);
        if ((bits & XBR_SHALLOW) != 0u) {
            a = max(a, xbrCover(p, vec2(1.0, 2.0), 2.0, k));
        }
        if ((bits & XBR_STEEP) != 0u) {
            a = max(a, xbrCover(p, vec2(2.0, 1.0), 2.0, k));
        }
        if (a > 0.0) {
            ivec2 o = (bits & XBR_FROM_H) != 0u ? ivec2(0, sy) : ivec2(sx, 0);
            col = mix(col, xbrPixel(clamp(e + o, ivec2(0), size - 1)), a);
        }
    }
    return col;
}
