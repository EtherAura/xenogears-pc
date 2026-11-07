#version 450
#extension GL_GOOGLE_include_directive : require

// One PS1 GPU pixel: texture lookup through the texture page and CLUT,
// modulation, dithering, then the read-modify-write of VRAM (mask check,
// semi-transparency, mask bit). VRAM is a storage image; with INTERLOCK the
// read-modify-write runs in primitive order per pixel, as on the PS1.
//
// Built with WIDE: the same pixel into the wide target (port/src/gpu/wide.c)
// instead of VRAM; texels still come from VRAM, except 15-bit ones the
// primitive takes from a screen capture or frame buffer (vWideTex), which
// come from the wide target.

#ifdef INTERLOCK
#extension GL_ARB_fragment_shader_interlock : require
layout(pixel_interlock_ordered) in;
#define BEGIN_ORDERED() beginInvocationInterlockARB()
#define END_ORDERED() endInvocationInterlockARB()
#else
#define BEGIN_ORDERED()
#define END_ORDERED()
#endif

// GpuVertex.mode (port/src/gpu/gpu_internal.h)
const uint DRAW_TEXTURED = 0x001u;
const uint DRAW_RAW = 0x002u;
const uint DRAW_SEMI = 0x004u;
const uint DRAW_FADE = 0x8000u; // PC: every texel blended half and half (pcFadePrim)
const uint DRAW_DITHER = 0x020u;
const uint DRAW_SET_MASK = 0x040u;
const uint DRAW_CHECK_MASK = 0x080u;
const uint DRAW_FILL = 0x100u;
const uint DRAW_GOURAUD = 0x200u;
const uint DRAW_CACHED = 0x2000u;

layout(set = 0, binding = 0, r16ui) uniform coherent uimage2D vram;
#ifdef WIDE
layout(set = 0, binding = 2, r16ui) uniform coherent uimage2D wideImg;
#define DST wideImg
#else
#define DST vram
#endif

// Triangles walked in the PS1's fixed point (gp0.c buildSpans): at vSpan a
// header {first y, rows, du, dv}, {dr, dg, db, start row}, then per row
// {xmin, xmax, u, v}, {r, g, b, decoded texels}, all 16.16 at xmin.
layout(set = 0, binding = 1, std430) readonly buffer Spans {
    ivec4 spans[];
};

#ifdef ORDERED_LIST
layout(location = 10) flat in uint vOrder;
#endif

#include "prompt.glsl"
#ifdef ORDERED_LIST
#define ORDERED_COLLECT
#include "ordered.glsl"
#endif

layout(location = 0) noperspective in vec2 vUV;
layout(location = 1) noperspective in vec3 vColor;
layout(location = 2) flat in uint vMode;
layout(location = 3) flat in uint vTex;
layout(location = 4) flat in uint vTexWindow;
layout(location = 5) flat in uint vClipMin;
layout(location = 6) flat in uint vClipMax;
layout(location = 7) flat in uint vSpan;
#ifdef WIDE
layout(location = 8) flat in uint vWideTex;
layout(location = 9) flat in uint vWideEdge; // the 4:3 draw area's x0 | x1 << 16
#endif

// psx-spx "24bit RGB to 15bit RGB Dithering": the 4x4 matrix, added to the
// 8-bit colour, which is saturated and divided by 8.
const int dither[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);

uint fetch(int x, int y) {
    return imageLoad(vram, ivec2(x & 1023, y & 511)).r;
}

uvec3 unpack5(uint c) {
    return uvec3(c & 31u, (c >> 5) & 31u, (c >> 10) & 31u);
}

uint pack5(uvec3 c) {
    return c.r | (c.g << 5) | (c.b << 10);
}

// Blend an already saturated, quantized foreground with 5-bit VRAM, per
// channel, saturated to 0..31 (psx-spx "Semi-transparency"; that the
// foreground is quantised first was measured black-box, M-G6).
uvec3 blend5(uint mode, uvec3 b, ivec3 f) {
    ivec3 bi = ivec3(b);
    ivec3 o;
    if (mode == 0u) {
        o = (bi + f) >> 1;
    } else if (mode == 1u) {
        o = bi + f;
    } else if (mode == 2u) {
        o = bi - f;
    } else {
        o = bi + (f >> 2);
    }
    return uvec3(clamp(o, 0, 31));
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    uint mode = vMode;
    uint semiMode = (mode >> 3) & 3u;
    bool write = true;
    bool semi = (mode & DRAW_SEMI) != 0u;
    bool dithered = (mode & DRAW_DITHER) != 0u;
    uint maskOut = (mode & DRAW_SET_MASK) != 0u ? 0x8000u : 0u;
    ivec3 c8 = ivec3(floor(vColor + 0.5));
    int d = dither[(p.y & 3) * 4 + (p.x & 3)];
    ivec2 uvFixed = ivec2(0);
    uint cachedTexel = 0u;
    bool spanned = vSpan != 0xFFFFFFFFu;
    if (spanned) {
        ivec4 h0 = spans[vSpan];
        int row = p.y - h0.x;
        if (row < 0 || row >= h0.y) {
            write = false;
        } else {
            ivec4 h1 = spans[vSpan + 1u];
            ivec4 r0 = spans[vSpan + 2u + uint(row) * 2u];
            ivec4 r1 = spans[vSpan + 3u + uint(row) * 2u];
            if (p.x < r0.x || p.x > r0.y) {
                write = false;
            }
            int k = p.x - r0.x;
            if ((mode & DRAW_CACHED) != 0u && write) {
                uint word = uint(r1.w + k);
                cachedTexel = uint(spans[vSpan + word / 4u][word & 3u]);
            }
            uvFixed = ivec2(r0.z + k * h0.z, r0.w + k * h0.w) >> 16;
            if ((mode & DRAW_GOURAUD) != 0u) {
                // Gouraud colour changes at every pixel (measured, buildSpans).
                c8 = ivec3(r1.x + k * h1.x, r1.y + k * h1.y, r1.z + k * h1.z) >> 16;
            }
        }
    }
    // Modulation and dither precede saturation and 5-bit blending (psx-spx
    // "Modulation", dithering; the order measured black-box, M-G6).
    ivec3 front8 = ivec3(0);
    uint fill = 0u;

    if ((mode & DRAW_FILL) != 0u) {
        fill = pack5(uvec3(c8) >> 3);
        semi = false;
        dithered = false;
        maskOut = 0u;
    } else {
        ivec2 cmin = ivec2(vClipMin & 0xFFFFu, vClipMin >> 16);
        ivec2 cmax = ivec2(vClipMax & 0xFFFFu, vClipMax >> 16);
        if (any(lessThan(p, cmin)) || any(greaterThan(p, cmax))) {
            write = false;
        }
        if ((mode & DRAW_TEXTURED) != 0u) {
            ivec2 uv = (spanned ? uvFixed : ivec2(floor(vUV + 0.5))) & 0xFF;
            // Texture window (GP0 E2h): mask and offset in 8-texel steps.
            ivec2 twMask = ivec2(vTexWindow & 0x1Fu, (vTexWindow >> 5) & 0x1Fu) * 8;
            ivec2 twOffset = ivec2((vTexWindow >> 10) & 0x1Fu, (vTexWindow >> 15) & 0x1Fu) * 8;
            uv = (uv & ~twMask) | (twOffset & twMask);
            uint tpage = vTex & 0xFFFFu;
            uint clut = vTex >> 16;
            int tx = int(tpage & 0xFu) * 64;
            int ty = int((tpage >> 4) & 1u) * 256;
            int cx = int(clut & 0x3Fu) * 16;
            int cy = int((clut >> 6) & 0x1FFu);
            uint depth = (tpage >> 7) & 3u;
            uint texel;
            if ((mode & DRAW_CACHED) != 0u) {
                texel = cachedTexel;
            } else if ((tpage & ~0x60u) == 0x8180u) {
                texel = promptTexel(clut,uint(uv.x),uint(uv.y));
            } else if (depth == 0u) {
                uint w = fetch(tx + (uv.x >> 2), ty + uv.y);
                texel = fetch(cx + int((w >> ((uv.x & 3) * 4)) & 0xFu), cy);
            } else if (depth == 1u) {
                uint w = fetch(tx + (uv.x >> 1), ty + uv.y);
                texel = fetch(cx + int((w >> ((uv.x & 1) * 8)) & 0xFFu), cy);
            } else {
#ifdef WIDE
                // A texel past VRAM's right edge wraps to its left edge, as on
                // the PS1, inside the 4:3 draw area, so the centre stays the
                // 4:3 picture. Beside it the wide copy goes on where VRAM
                // ends: a capture at VRAM's right edge (battle's at 0x2C0)
                // continues into its right margin, which the pieces of its
                // last column reach one texel past the edge.
                bool beside = p.x < int(vWideEdge & 0xFFFFu) || p.x > int(vWideEdge >> 16);
                if (vWideTex != 0u && (tx + uv.x < 1024 || beside)) {
                    // WIDE_TEX(dx, dy): 16-bit dx, 15-bit dy, bit 31 set.
                    int dx = int(vWideTex << 16) >> 16;
                    int dy = int(vWideTex << 1) >> 17;
                    texel = imageLoad(wideImg, ivec2((tx + uv.x + dx) & 2047, (ty + uv.y + dy) & 2047)).r;
                } else {
                    texel = fetch(tx + uv.x, ty + uv.y);
                }
#else
                texel = fetch(tx + uv.x, ty + uv.y);
#endif
            }
            // 0000h is transparent; bit 15 marks the semi-transparent texels
            // (psx-spx "Texture Color Black Limitations", "Semi-transparency")
            // and is written as the mask bit (psx-spx "GP0(E6h)").
            if (texel == 0u) {
                write = false;
            }
            if ((texel & 0x8000u) == 0u && (mode & DRAW_FADE) == 0u) {
                semi = false;
            }
            maskOut |= texel & 0x8000u;
            // Modulation: texel * colour / 128 (a raw texture is colour 128).
            ivec3 m = (mode & DRAW_RAW) != 0u ? ivec3(128) : c8;
            ivec3 t = ivec3(unpack5(texel));
            front8 = (t * m) >> 4;
        } else {
            front8 = c8;
        }
    }
    ivec3 front = clamp((front8 + (dithered ? d : 0)) >> 3, 0, 31);

#ifdef ORDERED_LIST
    if(write) {
        uint flags=orderedFlags(mode,semi,semiMode,maskOut!=0u)|ORD_QUANT5;
        orderedAppend(p,(mode&DRAW_FILL)!=0u?fill:pack5(uvec3(front)),flags);
    }
#else
    BEGIN_ORDERED();
    if (write) {
        uint dst = imageLoad(DST, p).r;
        if ((mode & DRAW_CHECK_MASK) == 0u || (dst & 0x8000u) == 0u) {
            uint color;
            if ((mode & DRAW_FILL) != 0u) {
                color = fill;
            } else if (semi) {
                color = pack5(blend5(semiMode, unpack5(dst), front));
            } else {
                color = pack5(uvec3(front));
            }
            imageStore(DST, p, uvec4(color | maskOut));
        }
    }
    END_ORDERED();
#endif
}
