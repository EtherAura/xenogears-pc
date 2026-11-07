#version 450
#extension GL_GOOGLE_include_directive : require

// The TV picture: the display area of VRAM, 15-bit or 24-bit, scaled into
// the destination rectangle of the target (port/src/gpu/present.c chooses
// it), with the display options' note on top.
//
// The filter (filters.glsl) resamples the picture to the destination:
// nearest by default, exact in integers (target pixel i shows source pixel
// floor((i + 0.5) * source / destination), so a whole-number scale gives
// every PS1 pixel the same block); linear, sharp (area), bicubic or xbr
// (edges in pixel art) as present.c chooses.
//
// Wide aspects (port/src/gpu/wide.c): the picture comes from the wide target
// (src 1), or, for a frame that stays 4:3, the sides of the wide frame (clip)
// beside the picture show it blurred and darkened (fill 1) or black
// (widefill=zoom is only a larger destination, present.c).

layout(set = 0, binding = 0, r16ui) uniform readonly uimage2D vram;
layout(set = 0, binding = 2, r16ui) uniform readonly uimage2D wideImg;
// The hi-res copy (port/src/gpu/hires.c): src 2 its VRAM, 3 its wide target,
// disp then in its pixels (8:8:8, mask bit 24).
layout(set = 0, binding = 3, r32ui) uniform readonly uimage2D hiVram;
layout(set = 0, binding = 4, r32ui) uniform readonly uimage2D hiWide;
// A movie pack's picture (port/src/gpu/moviepack.c), src 4: 8:8:8; disp is
// then in its pixels, disp.xy where the picture's corner sits in the area.
layout(set = 1, binding = 0, r32ui) uniform readonly uimage2D movieImg;

// The note's 1-bit bitmap and the picture prepass's words (below), in the
// frame's span buffer (vk.c).
#ifdef PICTURE_PREPASS
layout(set = 0, binding = 1, std430) buffer Osd {
#else
layout(set = 0, binding = 1, std430) readonly buffer Osd {
#endif
    uint words[];
} osd;

layout(push_constant) uniform Display {
    ivec4 disp; // VRAM x, y, width, height
    ivec4 dest; // target x, y, width, height
    ivec4 osdRect; // the note: target x, y, width, height (width 0: none)
    ivec4 clip; // the picture shows only inside (4:3 TV: the TV's frame)
    int rgb24;
    int enabled;
    int filterMode; // FILTER_* (filters.glsl)
    int osdScale;
    int osdStride;
    int osdBase;
    int src;  // 0: VRAM; 1: the wide target; 2, 3: their hi-res copies; 4: a movie picture
    int fill; // beside a 4:3 picture in a wide frame: 0 black, 1 blurred picture
    int picture; // the picture prepass's pixels: their first word in the span buffer (-1: none)
    int codes;   // its xbr codes (-1: none)
} pc;

#ifdef PICTURE_PREPASS
layout(location = 0) out uint outCode;
#else
layout(location = 0) out vec4 outColor;
#endif

uint fetch(int x, int y) {
    return imageLoad(vram, ivec2(x & 1023, y & 511)).r;
}

// Byte n of the VRAM row y, counting from halfword x.
uint byteAt(int x, int y, int n) {
    return (fetch(x + (n >> 1), y) >> ((n & 1) * 8)) & 0xFFu;
}

// Pixel s of the display area, 0..255 per channel.
vec3 pixel(ivec2 s) {
    int y = pc.disp.y + s.y;
    uvec3 c;
    if (pc.src == 4) {
        ivec2 r = s - pc.disp.xy;
        if (all(greaterThanEqual(r, ivec2(0))) && all(lessThan(r, imageSize(movieImg)))) {
            uint w = imageLoad(movieImg, r).r;
            c = uvec3(w & 255u, (w >> 8) & 255u, (w >> 16) & 255u);
        } else {
            c = uvec3(0u);
        }
    } else if (pc.rgb24 != 0) {
        int n = s.x * 3;
        c = uvec3(byteAt(pc.disp.x, y, n), byteAt(pc.disp.x, y, n + 1), byteAt(pc.disp.x, y, n + 2));
    } else if (pc.src >= 2) {
        ivec2 size = pc.src == 2 ? imageSize(hiVram) : imageSize(hiWide);
        ivec2 a = ivec2((pc.disp.x + s.x) % size.x, y % size.y);
        uint w = pc.src == 2 ? imageLoad(hiVram, a).r : imageLoad(hiWide, a).r;
        c = uvec3(w & 255u, (w >> 8) & 255u, (w >> 16) & 255u);
    } else {
        uint w = pc.src != 0 ? imageLoad(wideImg, ivec2((pc.disp.x + s.x) & 2047, y & 2047)).r
                             : fetch(pc.disp.x + s.x, y);
        uvec3 c5 = uvec3(w & 31u, (w >> 5) & 31u, (w >> 10) & 31u);
        c = (c5 << 3) | (c5 >> 2);
    }
    return vec3(c);
}

// The picture prepass (vk.c), drawn over the picture's size (the target is a
// stand-in): xbr's codes for each picture pixel (filters.glsl), and on an
// integrated GPU each pixel once as 8:8:8 for the filters that read it many
// times; row after row into the span buffer, where the present pass reads
// them.
uint filterWord(int i) {
    return osd.words[i];
}

vec3 filterPixel(ivec2 s) {
#ifndef PICTURE_PREPASS
    if (pc.picture >= 0) {
        uint w = osd.words[pc.picture + s.y * pc.disp.z + s.x];
        return vec3(uvec3(w, w >> 8, w >> 16) & 255u);
    }
#endif
    return pixel(s);
}

#include "filters.glsl"

#ifdef PICTURE_PREPASS
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (all(lessThan(p, pc.disp.zw))) {
        int i = p.y * pc.disp.z + p.x;
        if (pc.picture >= 0) {
            uvec3 c = uvec3(pixel(p));
            osd.words[pc.picture + i] = c.r | c.g << 8 | c.b << 16;
        }
        if (pc.codes >= 0) {
            osd.words[pc.codes + i] = xbrLoad(p, pc.disp.zw) ? xbrCode() : 0u;
        }
    }
    outCode = 0u;
}
#else

// The side fill: the picture stretched across the whole frame, blurred
// (a 5 x 5 box of samples a 40th of its width apart) and darkened.
vec3 blurFill(ivec2 p) {
    vec2 s = vec2((float(p.x - pc.clip.x) + 0.5) / float(pc.clip.z) * float(pc.disp.z),
                  (float(p.y - pc.dest.y) + 0.5) / float(pc.dest.w) * float(pc.disp.w));
    float step = float(pc.disp.z) / 40.0;
    ivec2 last = pc.disp.zw - 1;
    vec3 sum = vec3(0.0);
    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            sum += pixel(clamp(ivec2(s + vec2(i, j) * step), ivec2(0), last));
        }
    }
    return sum / 25.0 * 0.45;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 f = p - pc.dest.xy;
    vec3 col = vec3(0.0);
    ivec2 c = p - pc.clip.xy;
    if (pc.enabled != 0 && all(greaterThanEqual(f, ivec2(0))) && all(lessThan(f, pc.dest.zw)) &&
        all(greaterThanEqual(c, ivec2(0))) && all(lessThan(c, pc.clip.zw))) {
        col = filterPicture(pc.filterMode, f, pc.disp.zw, pc.dest.zw, pc.codes);
    } else if (pc.enabled != 0 && pc.fill != 0 && f.y >= 0 && f.y < pc.dest.w && all(greaterThanEqual(c, ivec2(0))) &&
               all(lessThan(c, pc.clip.zw))) {
        col = blurFill(p);
    }
    ivec2 o = p - pc.osdRect.xy;
    if (pc.osdRect.z > 0 && all(greaterThanEqual(o, ivec2(0))) && all(lessThan(o, pc.osdRect.zw))) {
        ivec2 b = o / pc.osdScale;
        int i = b.y * pc.osdStride + b.x;
        uint w = osd.words[pc.osdBase + (i >> 5)];
        col = ((w >> uint(i & 31)) & 1u) != 0u ? vec3(255.0, 250.0, 225.0) : col * 0.3;
    }
    outColor = vec4(col / 255.0, 1.0);
}
#endif
