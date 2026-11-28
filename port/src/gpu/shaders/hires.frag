#version 450
#extension GL_GOOGLE_include_directive : require

// One pixel of the hi-res copy (port/src/gpu/hires.c): what draw.frag does
// for a PS1 pixel, for each of the N x N pixels of the copy, from a real
// triangle the device rasterises (hires.vert has the sample grid).
//
// What stays exact: CLUTs and 4- and 8-bit texels come from the 1x VRAM;
// 15-bit texels (screen captures, frame buffers, uploaded pictures) from
// the copy itself, N x N per texel. Texture coordinates are clamped to the
// texels the 1x pass sampled (vUVMin, vUVMax), so an edge never shows the
// neighbouring image in the same texture page.
//
// The copy's pixels: 8:8:8 colour and the mask bit (bit 24). Colour
// (push constant): 0 true colour; 1 15-bit with the PS1's dither, each 1x
// pixel's index; 2 15-bit, the dither index of each hi-res pixel. True
// colour keeps what each primitive adds as the PS1 computes it (its 5-bit
// colour: a dim glow layer the PS1 drops stays dropped, so blacks stay
// black) and gains the precision the PS1 loses after that: blending results
// stay 8-bit, and shading the PS1 dithers is smooth, at the average of its
// dither pattern. 3 raw 8-bit colour: every colour in 8 bits from the texel
// on, no dither (dark areas come out brighter than the PS1's: the dim parts
// of glow layers it truncates away add up).
//
// Built with WIDE: into the hi-res wide target; 15-bit texels the primitive
// takes from the wide target (vWideTex) come from its hi-res copy.
//
// Built with TILE_IMAGE (VK_EXT_shader_tile_image, as draw.frag's): the copy
// is the pass's colour attachment and the pixel's own word is read from tile
// memory in primitive order (vk.c tileHiPass). Texels still come from the
// storage images: a batch never reads what it writes (gp0.c batchAdd,
// batchAddWide). One pixel a fragment, at the copy's own position (the
// vertices' rasterShift is 0): not with COARSE. With DEPTH as well (a
// registered scene) the pass has a second attachment, the scene's depth
// plane in the copy's pixel grid (vk.c tileHiDepthPass), read and written in
// the same primitive order.

#ifdef INTERLOCK
#extension GL_ARB_fragment_shader_interlock : require
layout(pixel_interlock_ordered) in;
#define BEGIN_ORDERED() beginInvocationInterlockARB()
#define END_ORDERED() endInvocationInterlockARB()
#else
#define BEGIN_ORDERED()
#define END_ORDERED()
#endif

#ifdef TILE_IMAGE
#extension GL_EXT_shader_tile_image : require
layout(location = 0) tileImageEXT uattachmentEXT tilePixel;
layout(location = 0) out uint outPixel;
#ifdef DEPTH
layout(location = 1) tileImageEXT uattachmentEXT tileDepth;
layout(location = 1) out uint outDepth;
#endif
#endif

#ifdef ORDERED_LIST
layout(location = 12) flat in uint vOrder;
#endif

#include "prompt.glsl"
#include "hdr.glsl"
#ifdef ORDERED_LIST
#define ORDERED_COLLECT
#ifdef DEPTH
#define ORDERED_DEPTH
#endif
#include "ordered.glsl"
#endif

// GpuVertex.mode (port/src/gpu/gpu_internal.h)
const uint DRAW_RESOLVED = 0x4000u;
const uint DRAW_TEXTURED = 0x001u;
const uint DRAW_RAW = 0x002u;
const uint DRAW_SEMI = 0x004u;
const uint DRAW_FADE = 0x8000u; // PC: every texel blended half and half (pcFadePrim)
const uint DRAW_DITHER = 0x020u;
const uint DRAW_SET_MASK = 0x040u;
const uint DRAW_CHECK_MASK = 0x080u;
const uint DRAW_FILL = 0x100u;
const uint DRAW_PRECISE = 0x400u;
const uint DRAW_SURFACE = 0x800u;
const uint DRAW_FRINGE = 0x1000u;
const uint DRAW_HDR_EMISSION = 0x10000u;

const uint MASK = 0x1000000u; // the hi-res pixel's mask (STP) bit
// Display-only coverage, independent of the PS1 mask bit. Original faces
// always draw in game order; seam strips only fill uncovered pixels.
const uint COVERED = 0x2000000u;

layout(set = 0, binding = 0, r16ui) uniform readonly uimage2D vram;
layout(set = 0, binding = 3, r32ui) uniform coherent uimage2D hiVram;
layout(set = 0, binding = 4, r32ui) uniform coherent uimage2D hiWide;
layout(set = 0, binding = 6, r32ui) uniform readonly uimage2D replayCache;
#ifdef WIDE
#define DST hiWide
#define REPLAY_SOURCE hiVram
#else
#define DST hiVram
#define REPLAY_SOURCE hiWide
#endif

layout(push_constant) uniform Hi {
    int n;
    int color;
    ivec2 target;
    ivec2 wideSize;
    ivec2 ditherOrigin;
    int rasterScale; // samples per native pixel in the COARSE build
#if defined(DEPTH) || defined(MSAA)
    ivec2 depthOrigin; // the depth image's pixel (0, 0) in the target (hi-res pixels)
    ivec2 depthSize;   // its size
#endif
#ifdef MSAA
    int sampleX;
    int sampleY;
    int sampleStride;
    uint sampleEpoch;
#endif
} pc;
#ifdef MSAA
// Rasterizer coverage at one shade per pixel. Keep the packed sample words
// until this ordered run ends so adjacent faces and translucent overlaps
// see each sample's original colour, mask and seam coverage.
layout(constant_id = 1) const uint sampleCount = 4u;
layout(set = 2, binding = 0, std430) coherent buffer SampleStamps { uint sampleStamps[]; };
layout(set = 2, binding = 1, std430) coherent buffer SceneSamples { uint sceneSamples[]; };
#endif
#ifdef TILE_IMAGE
// At Auto's 1x level, texel and output grids coincide. Specializing this
// uniform eliminates integer divisions for both axes of every sample.
layout(constant_id = 0) const bool unitScale = false;
#define HI_SCALE (unitScale ? 1 : pc.n)
#else
#define HI_SCALE pc.n
#endif

#ifdef DEPTH
// The scene's depth image (scene.c, vk.c gvkSceneBegin): for each pixel of
// the frame being drawn, the view depth of what replaced it last, in 1/16
// units, with DEPTH_FIGURE when that was a sprite; DEPTH_SHARP a picture
// kept in focus; DEPTH_FAR nothing drawn yet.
// Built with DEPTH only while a registered scene is drawn: without it this
// shader is the one it always was.
layout(set = 1, binding = 0, r32ui) uniform coherent uimage2D sceneDepth;
layout(location = 14) noperspective in float vInvZ;
const uint DEPTH_SHARP = 0u;
const uint DEPTH_FAR = 0xFFFFFFFFu;
const uint DEPTH_FIGURE = 0x40000000u;
// The depth a fragment that replaces its pixel gives it.
uint depthWord() {
    return vInvZ > 1.5 ? DEPTH_SHARP
                       : uint(clamp(16.0 / abs(vInvZ) + 0.5, 1.0, 1.0e9)) | (vInvZ < 0.0 ? DEPTH_FIGURE : 0u);
}
#endif

layout(location = 0) in vec2 vUV;
layout(location = 1) noperspective in vec3 vColor;
layout(location = 2) flat in uint vMode;
layout(location = 3) flat in uint vTex;
layout(location = 4) flat in uint vTexWindow;
layout(location = 5) flat in uint vClipMin;
layout(location = 6) flat in uint vClipMax;
layout(location = 7) flat in uint vUVMin;
layout(location = 8) flat in uint vUVMax;
layout(location = 11) flat in uvec4 vSample;
layout(location = 13) flat in ivec2 vRasterShift;
layout(set = 0, binding = 1, std430) readonly buffer Samples { uint pixels[]; };
// Texture pack images kept on the GPU (vk.c resPlace): vSample.w bit 16.
layout(set = 0, binding = 8, std430) readonly buffer Kept { uint kept[]; };
layout(location = 9) flat in uint vWideTex;
layout(location = 10) flat in uint vWideEdge;

const int dither[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);

uint fetch(int x, int y) {
    return imageLoad(vram, ivec2(x & 1023, y & 511)).r;
}

ivec3 rgb8(uint w) {
    return ivec3(int(w & 255u), int((w >> 8) & 255u), int((w >> 16) & 255u));
}

uint pack8(ivec3 c) {
    return uint(c.r) | uint(c.g) << 8 | uint(c.b) << 16;
}

// Saturated 5-bit channels widened as the PS1's DAC shows them.
ivec3 expand5(ivec3 c) {
    return (c << 3) + (c >> 2);
}

// A 15-bit VRAM word as a hi-res pixel.
uint word15(uint c) {
    ivec3 c5 = ivec3(int(c & 31u), int((c >> 5) & 31u), int((c >> 10) & 31u));
    return pack8(expand5(c5)) | ((c & 0x8000u) != 0u ? MASK : 0u);
}

ivec2 floorDiv(ivec2 a, int b) {
    return ivec2(a.x >= 0 ? a.x / b : -((-a.x + b - 1) / b), a.y >= 0 ? a.y / b : -((-a.y + b - 1) / b));
}

uvec3 blend5(uint mode, ivec3 b, ivec3 f) {
    ivec3 o;
    if (mode == 0u) {
        o = (b + f) >> 1;
    } else if (mode == 1u) {
        o = b + f;
    } else if (mode == 2u) {
        o = max(b - f, ivec3(0));
    } else {
        o = b + (f >> 2);
    }
    return uvec3(min(o, ivec3(31)));
}

ivec3 blend8(uint mode, ivec3 b, ivec3 f) {
    ivec3 o;
    if (mode == 0u) {
        o = (b + f) >> 1;
    } else if (mode == 1u) {
        o = b + f;
    } else if (mode == 2u) {
        o = max(b - f, ivec3(0));
    } else {
        o = b + (f >> 2);
    }
    return clamp(o, ivec3(0), ivec3(255));
}

// True colour for what the PS1 dithers (shaded primitives): the average of
// its dither pattern over v, without the pattern. The PS1 adds -4..3 and
// drops the low 3 bits (psx-spx "24bit RGB to 15bit RGB Dithering"), so its pattern averages about 4 below v; a plain v
// would lift every dark shaded layer (fog, haze) above the PS1's black.
ivec3 ditherAverage(ivec3 v) {
    ivec3 sum = ivec3(0);
    for (int d = -4; d <= 3; d++) {
        sum += expand5(clamp(v + d, 0, 255) >> 3);
    }
    return (sum + 4) >> 3;
}

// RGBA snapshots are immutable for this draw. Alpha encodes clear/STP,
// not translucency: both 128 and 255 contribute full colour coverage.
// A sample's pixel: RGBA (A 0 clear, 128 STP, 255 opaque) from the span
// buffer or the kept images (bit 16); an index map (bit 17, a byte a pixel)
// is coloured with the primitive's live CLUT, as the 1x texel would be.
// Private replay (bit 18) instead owns an exact RGBA palette before the
// indices; the high byte stores its length minus one. No live CLUT is read.
// With a kept index map, the palette follows a word pointing to that map.
uint samplePixel(ivec2 t, ivec2 lo, ivec2 hi, int scale) {
    ivec2 origin = ivec2(int(vSample.y << 16) >> 16, int(vSample.y) >> 16);
    ivec2 size = ivec2(vSample.z & 65535u, vSample.z >> 16);
    t = clamp(t, lo*scale, hi*scale + (scale-1)) - origin*scale;
    t = clamp(t, ivec2(0), size*scale-1);
    uint i = uint(t.y*size.x*scale + t.x);
    bool fromKept = (vSample.w & 0x10000u) != 0u;
    if (!fromKept && (vSample.w & 0x800000u) != 0u) {
        uint c=pixels[vSample.x+(i>>1)]>>((i&1u)*16u)&65535u;
        if(c==0u) {
            uint count=uint(size.x*size.y*scale*scale);
            bool black=(vSample.w&0x1000000u)!=0u && ((pixels[vSample.x+(count+1u)/2u+(i>>5)]>>(i&31u))&1u)!=0u;
            return black?0xff000000u:0u;
        }
        uvec3 c5=uvec3(c&31u,c>>5&31u,c>>10&31u),c8=c5<<3|c5>>2;
        return c8.r|c8.g<<8|c8.b<<16|((c&32768u)!=0u?128u:255u)<<24;
    }
    if ((vSample.w & 0x20000u) == 0u) {
        return fromKept ? kept[vSample.x + i] : pixels[vSample.x + i];
    }
    bool ownedPalette = (vSample.w & 0x40000u) != 0u;
    uint paletteSize = ownedPalette ? (vSample.w >> 24) + 1u : 0u;
    uint keptBase = fromKept && ownedPalette ? pixels[vSample.x] : vSample.x;
    uint w = fromKept ? kept[keptBase + (i >> 2)] : pixels[vSample.x + paletteSize + (i >> 2)];
    uint index = (w >> ((i & 3u) * 8u)) & 255u;
    if (ownedPalette) return pixels[vSample.x + uint(fromKept) + index];
    uint clut = vTex >> 16;
    uint c = fetch(int(clut & 0x3Fu) * 16 + int(index), int((clut >> 6) & 0x1FFu));
    if (c == 0u) {
        return 0u;
    }
    uvec3 c5 = uvec3(c & 31u, (c >> 5) & 31u, (c >> 10) & 31u);
    uvec3 c8 = (c5 << 3) | (c5 >> 2);
    return c8.r | c8.g << 8 | c8.b << 16 | ((c & 0x8000u) != 0u ? 128u : 255u) << 24;
}
// Patch-relative taps, clamped again by samplePixel to the actual snapshot.
// Alpha is coverage here; STP is kept separately from the nearest texel.
ivec2 edgeLo, edgeHi;
vec4 xbrPixel(ivec2 p) {
    uint c = samplePixel(p + edgeLo, edgeLo, edgeHi, 1);
    return (c >> 24) != 0u ? vec4(rgb8(c), 255.0) : vec4(0.0);
}
#include "edge.glsl"

// Enhanced text with smooth edges (TEXTURE_FILTER_TEXT_SMOOTH): the fraction
// of the copy's pixel the overlay covers, 1 for everything else. The write
// blends the drawn colour over what is there by it.
float textCover = 1.0;

// The texel boundary nudge (filteredPixel's comment), none at 1x.
float boundaryNudge(int scale) { return scale > 1 ? 1.0/256.0 : 0.0; }

vec4 linearTexels(vec2 pos, ivec2 lo, ivec2 hi, int scale) {
    pos -= 0.5;
    ivec2 base = ivec2(floor(pos));
    vec2 f = fract(pos);
    vec3 rgb = vec3(0);
    float coverage = 0;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        uint tap = samplePixel(base + ivec2(x, y), lo, hi, scale);
        float weight = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
        if ((tap >> 24) != 0u) { rgb += vec3(rgb8(tap)) * weight; coverage += weight; }
    }
    return vec4(rgb, coverage);
}

vec4 mipTexel(uint base, uint levelOffset, ivec2 size, ivec2 p, bool levelZero, bool resident, bool owned) {
    p = clamp(p, ivec2(0), size - 1);
    uint index = levelOffset + uint(p.y * size.x + p.x);
    uint c;
    if (owned && (vWideTex & 0x40000u) != 0u) {
        // The snapshot owns one lossless palette for the entire lower chain.
        uint colors = (vWideTex >> 24) + 1u;
        uint word = pixels[base + colors + (index >> 2)];
        c = pixels[base + ((word >> ((index & 3u) * 8u)) & 255u)];
    } else {
        c = resident ? kept[base + index] : pixels[base + index];
    }
    uint alpha = c >> 24;
    if (alpha == 0u) return vec4(0.0);
    if (levelZero) alpha = 255u;
    return vec4(rgb8(c), float(alpha));
}

vec4 mipLinear(uint base, uint levelOffset, ivec2 size, vec2 p, ivec2 lo, ivec2 hi, bool levelZero, bool resident, bool owned) {
    p -= 0.5;
    ivec2 t = ivec2(floor(p));
    vec2 w = fract(p);
    return mix(mix(mipTexel(base, levelOffset, size, clamp(t, lo, hi), levelZero, resident, owned),
                   mipTexel(base, levelOffset, size, clamp(t + ivec2(1, 0), lo, hi), levelZero, resident, owned), w.x),
               mix(mipTexel(base, levelOffset, size, clamp(t + ivec2(0, 1), lo, hi), levelZero, resident, owned),
                   mipTexel(base, levelOffset, size, clamp(t + 1, lo, hi), levelZero, resident, owned), w.x), w.y);
}

uint anisotropicPixel(vec2 at, vec2 dx, vec2 dy, ivec2 lo, ivec2 hi, uint sampling, out bool clear) {
    bool pack = (sampling & 64u) != 0u;
    bool owned = (vSample.w & 0x200000u) != 0u;
    bool baseOnly = (pack || owned) && (vWideEdge & 0x40000000u) != 0u;
    int imageScale = int((vSample.w >> 8) & 255u);
    at *= float(imageScale);
    dx *= float(imageScale);
    dy *= float(imageScale);
    uint nearest = samplePixel(ivec2(floor(at + boundaryNudge(imageScale))), lo, hi, imageScale);
    clear = (nearest >> 24) == 0u;
    uint stp = (nearest >> 24) == 128u ? MASK : 0u;
    uint original = (nearest & 0xFFFFFFu) | stp;
    if (pack && (sampling & 32u) != 0u) {
        vec4 bilinear = linearTexels(at, lo, hi, imageScale);
        clear = bilinear.a < 0.5;
        if (!clear) original = pack8(ivec3(floor(bilinear.rgb / bilinear.a + 0.5))) | stp;
    }
    if (clear) return 0u; // keep the base image's coverage and STP

    ivec2 origin = ivec2(int(vSample.y << 16) >> 16, int(vSample.y) >> 16);
    ivec2 size = ivec2(vSample.z & 65535u, vSample.z >> 16);
    // Ellipse of the screen pixel in texture space: the eigenvalues of
    // J*transpose(J) give its squared axes, including perspective/shear.
    vec2 u = vec2(dx.x, dy.x), v = vec2(dx.y, dy.y);
    float uu = dot(u, u), vv = dot(v, v), uv = dot(u, v);
    float major2 = 0.5 * (uu + vv + length(vec2(uu - vv, 2.0 * uv)));
    float major = sqrt(major2);
    if (isnan(major) || isinf(major) || (major <= 1.0 && (pack || (sampling & 32u) == 0u)))
        return original;
    float minor = abs(dx.x * dy.y - dx.y * dy.x) / max(major, 1e-6);
    int limit = int(sampling & 15u);
    float width = max(1.0, max(minor, major / float(limit)));
    int count = int(clamp(ceil(major / (baseOnly ? 1.0 : width)), 1.0, float(limit)));
    vec2 direction = uu >= vv ? vec2(major2 - vv, uv) : vec2(uv, major2 - uu);
    direction *= major / max(length(direction), 1e-6);

    // Fixed-colour replacement mips share a whole image across faces and
    // may only read complete blocks inside this face. Indexed replacement
    // and native mips are built for the face's patch.
    uint base = pack || owned ? vWideEdge & 0x3FFFFFFFu : vSample.x;
    bool resident = !owned && (pack ? (vWideEdge & 0x80000000u) != 0u : (vSample.w & 0x10000u) != 0u);
    bool sharedPage = pack && (owned ? (vWideEdge & 0x80000000u) != 0u : (vSample.w & 0x20000u) == 0u);
    if ((pack || owned) && !sharedPage) {
        ivec2 start = max(origin, lo);
        size = min(origin + size, hi + 1) - start;
        origin = start;
    }
    size *= imageScale;
    origin *= imageScale;
    ivec2 fullSize = size;
    ivec2 patchStart = max(ivec2(0), lo * imageScale - origin);
    ivec2 patchEnd = min(size, (hi + 1) * imageScale - origin);
    ivec2 firstTap = ivec2(0), lastTap = size - 1;
    int lod = baseOnly ? 0 : int(clamp(floor(log2(width) + 0.5), 0.0, 11.0));
    int level = 0;
    uint levelOffset = 0u;
    for (; level < lod && any(greaterThan(size, ivec2(1))); level++) {
        ivec2 next = (size + 1) / 2;
        if (sharedPage) {
            int block = 2 << level;
            ivec2 first = (patchStart + block - 1) / block;
            ivec2 last = mix(patchEnd / block, next, equal(patchEnd, fullSize)) - 1;
            if (any(greaterThan(first, last))) break;
            firstTap = first;
            lastTap = last;
        } else {
            lastTap = next - 1;
        }
        if ((!pack && !owned) || level > 0) levelOffset += uint(size.x * size.y);
        size = next;
    }
    float scale = exp2(float(-level));
    vec2 center = (at - vec2(origin)) * scale;
    vec2 step = direction * (scale / float(count));
    vec2 tap = center - 0.5 * float(count - 1) * step;
    vec4 sum = vec4(0.0);
    for (int i = 0; i < count; ++i, tap += step) {
        vec2 pos = clamp(tap, vec2(0), vec2(size));
        if ((pack || owned) && level == 0) {
            vec4 value = linearTexels(pos + vec2(origin), lo, hi, imageScale);
            sum += vec4(value.rgb, value.a * 255.0);
        } else {
            sum += mipLinear(base, levelOffset, size, pos, firstTap, lastTap, level == 0, resident, owned);
        }
    }
    if (sum.a <= 0.0) return original;
    return pack8(ivec3(clamp(floor(sum.rgb * (255.0 / sum.a) + 0.5), 0.0, 255.0))) | stp;
}

uint filteredPixel(vec2 at, ivec2 lo, ivec2 hi, out bool clear) {
    int scale = int((vSample.w >> 8) & 255u);
    if ((vSample.w & 255u) == 3u && scale == 1 && HI_SCALE <= 2) {
        // Exact textureSampleGet Scale2x rule, clamped to this face's patch.
        // RGB follows the selected corner; STP follows the nearest centre.
        ivec2 e=ivec2(floor(at)),sub=ivec2(floor(fract(at)*2.0));
        uint c=samplePixel(e,lo,hi,1),b=samplePixel(e+ivec2(0,-1),lo,hi,1);
        uint h=samplePixel(e+ivec2(0,1),lo,hi,1),l=samplePixel(e+ivec2(-1,0),lo,hi,1);
        uint r=samplePixel(e+ivec2(1,0),lo,hi,1),chosen=c;
        if(b!=h&&l!=r) {
            if(sub.y==0) { if(sub.x==0&&l==b)chosen=l;else if(sub.x!=0&&b==r)chosen=r; }
            else { if(sub.x==0&&l==h)chosen=l;else if(sub.x!=0&&h==r)chosen=r; }
        }
        clear=(chosen>>24)==0u;
        return (chosen&0xffffffu)|((c>>24)==128u?MASK:0u);
    }
    if ((vSample.w & 255u) == 3u && scale == 1 && HI_SCALE > 2) {
        edgeLo = lo; edgeHi = hi;
        ivec2 size = hi - lo + 1;
        vec2 pos = clamp(at - vec2(lo), vec2(0.0), vec2(size) - 0.00001);
        ivec2 e = ivec2(floor(pos));
        uint nearest = samplePixel(e + lo, lo, hi, 1);
        uint code = xbrLoad(e, size) ? xbrCode() : 0u;
        if (code == 0u) {
            clear = (nearest >> 24) == 0u;
            return (nearest & 0xffffffu) | ((nearest >> 24) == 128u ? MASK : 0u);
        }
        vec4 c = xbrBlend(e, size, code, fract(pos), vec2(HI_SCALE));
        clear = c.a < 127.5;
        ivec3 rgb = c.a > 0.0 ? ivec3(floor(c.rgb * (255.0 / c.a) + 0.5)) : ivec3(0);
        return pack8(clamp(rgb, ivec3(0), ivec3(255))) | ((nearest >> 24) == 128u ? MASK : 0u);
    }
    if ((vSample.w & 255u) == 4u) {
        // The overlay's opaque fraction over this pixel and the mean colour of
        // its opaque part, from 4x4 taps over a box a pixel and a half of the
        // copy wide (in the overlay: 1.5 pixels at 8x, 1 at 12x, 3 at 4x), and
        // never under one overlay pixel: an edge fades over about a pixel and
        // a half at any scale. (A box a pixel wide at 8x puts all four taps of
        // an axis inside one overlay pixel: the nearest result.) The face and
        // outline mix where they meet; the outer edge fades into the picture.
        // STP follows the nearest pixel.
        uint nearest = samplePixel(ivec2(floor(at*float(scale) + boundaryNudge(scale))),lo,hi,scale);
        float box = max(1.5 * float(scale) / float(HI_SCALE), 1.0);
        vec2 origin = at*float(scale) - 0.5*box;
        // The tap count comes from the push constants (always 4) so the loop
        // stays a loop: unrolled, its 16 samplePixel copies in every variant
        // made Mesa's pipeline compile take minutes.
        int taps = 4 + min(HI_SCALE, 0);
        vec3 rgb = vec3(0); float cover = 0.0, total = float(taps * taps);
        for (int y = 0; y < taps; ++y) for (int x = 0; x < taps; ++x) {
            uint tap = samplePixel(ivec2(floor(origin + (vec2(x, y) + 0.5) * (box / float(taps)))),lo,hi,scale);
            if ((tap >> 24) != 0u) { rgb += vec3(rgb8(tap)); cover += 1.0; }
        }
        textCover = cover / total;
        clear = textCover < 1.0/64.0;
        ivec3 color = cover > 0.0 ? ivec3(floor(rgb/cover + 0.5)) : ivec3(0);
        return pack8(color) | ((nearest >> 24) == 128u ? MASK : 0u);
    }
    if ((vSample.w & 255u) != 2u) { // nearest, or already scaled edge pixels
        // Where the copy's pixel pitch is a multiple of a texel's fraction
        // (12x over an 8x image: every third pixel; 4x over 8x: every
        // pixel) the sample point lands exactly on a texel boundary, and
        // the two rasterisations of one quad (the display copy's own and
        // the smooth-motion replay's, with other target sizes and origins)
        // interpolate uv a few ulps apart: they picked different texels
        // along the art's edges, a flicker on the cel portraits as the
        // host's pictures alternated. A 1/256 texel nudge (far below the
        // next sample position, far above the interpolation error) keeps
        // every boundary case on the same texel in both. Only for scaled
        // art: a 1x sample (an unmatched cel sprite keeps its native
        // pixels) must pick the texel the native path below does
        // (celgputest's unmatched check).
        uint edge = samplePixel(ivec2(floor(at*float(scale) + boundaryNudge(scale))),lo,hi,scale);
        clear = (edge >> 24) == 0u;
        return (edge & 0xffffffu) | ((edge >> 24) == 128u ? MASK : 0u);
    }
    uint nearest = samplePixel(ivec2(floor(at*float(scale) + boundaryNudge(scale))),lo,hi,scale); // the boundary nudge above
    vec4 bilinear = linearTexels(at * float(scale), lo, hi, scale);
    float coverage = bilinear.a;
    clear = coverage < 0.5;
    ivec3 color = coverage > 0.0 ? ivec3(floor(bilinear.rgb / coverage + 0.5)) : ivec3(0);
    return pack8(color) | ((nearest >> 24) == 128u ? MASK : 0u);
}

// Whether a fragment may replace the copy's pixel dst: seam strips (fringe)
// only fill uncovered pixels, and the PS1's mask check.
bool hiKeeps(uint dst, uint mode) {
    return ((mode & DRAW_FRINGE) == 0u || (dst & COVERED) == 0u) &&
           ((mode & DRAW_CHECK_MASK) == 0u || (dst & MASK) == 0u);
}

// The copy's new word at pixel pp (inside the 1x pixel q) over dst: colour
// in the push constant's mode, the mask bit and the display coverage.
uint hiPixel(uint dst, ivec2 pp, ivec2 q, uint mode, bool resolved, uint fill, bool raw, bool semi, uint semiMode,
             bool dithered, ivec3 front8, ivec3 front5, uint maskOut, uint emission) {
    uint color;
    if (resolved || (mode & DRAW_FILL) != 0u) {
        color = fill;
    } else if (raw) {
        color = pack8(semi ? blend8(semiMode, rgb8(dst), front8) : min(front8, ivec3(255)));
    } else if (pc.color == 0 && dithered) {
        ivec3 f = ditherAverage(front8);
        color = pack8(semi ? blend8(semiMode, rgb8(dst), f) : f);
    } else if (pc.color == 0) {
        ivec3 f = expand5(front5);
        ivec3 b = rgb8(dst);
        ivec3 v = !semi ? f : semiMode == 3u ? b + expand5(front5 >> 2) : blend8(semiMode, b, f);
        color = pack8(clamp(v, ivec3(0), ivec3(255)));
    } else if (dithered) {
        ivec2 dp = pc.color == 1 ? q + pc.ditherOrigin : pp + pc.ditherOrigin * HI_SCALE;
        int d = dither[(dp.y & 3) * 4 + (dp.x & 3)];
        ivec3 f = clamp((front8 + d) >> 3, 0, 31);
        color = pack8(expand5(semi ? ivec3(blend5(semiMode, rgb8(dst) >> 3, f)) : f));
    } else if (semi) {
        color = pack8(expand5(ivec3(blend5(semiMode, rgb8(dst) >> 3, front5))));
    } else {
        color = pack8(expand5(min(front5, ivec3(31))));
    }
    if (textCover < 1.0) { // smooth text: the edge over what is there
        ivec3 under = rgb8(dst), over = rgb8(color);
        color = pack8(clamp(ivec3(floor(mix(vec3(under), vec3(over), textCover) + 0.5)), ivec3(0), ivec3(255)));
    }
    uint coverage = dst & COVERED;
    if ((mode & (DRAW_SURFACE | DRAW_FRINGE)) != 0u) coverage = COVERED;
    else if (resolved || (mode & DRAW_FILL) != 0u) coverage = 0u;
    // Any ordinary draw, including a HUD pixel or fade, replaces the mark.
    if (emission != 0u) emission = max(emission, dst & HDR_MASK);
    return color | maskOut | coverage | emission;
}

void main() {
    bool evolving = (vSample.w & 0x100000u) != 0u;
    bool cachedCapture = (vSample.w & 0x400000u) != 0u;
    bool replayed = (evolving || cachedCapture) && (vSample.w & 0x200000u) == 0u && imageLoad(replayCache,ivec2(0,256)).r != 0u;
    int n = HI_SCALE;
    // Expand the reduced grid inside each native pixel. Rounding the two
    // boundaries up partitions even ratios such as a 2x raster in a 5x copy
    // without overlap or crossing native texture, dither or mask boundaries.
#ifdef COARSE
    int raster = pc.rasterScale;
    ivec2 grid = ivec2(gl_FragCoord.xy) - (vRasterShift / n) * raster;
    ivec2 q = (grid - ivec2(lessThan(grid, ivec2(0))) * (raster - 1)) / raster;
    ivec2 local = grid - q * raster;
    ivec2 begin = (local * n + raster - 1) / raster;
    ivec2 p = q * n + begin;
    ivec2 extent = ((local + 1) * n + raster - 1) / raster - begin;
    float step = float(n) / float(raster);
#else
    ivec2 p = ivec2(gl_FragCoord.xy) - vRasterShift;
    ivec2 q = p / n;
    const ivec2 extent = ivec2(1);
    const float step = 1.0;
#endif
    // Texture coordinates and colour as the PS1 samples them, centred on
    // each 1x pixel's block: the PS1 takes them at its sample point (the
    // block's corner, where coverage is decided) and rounds to the nearest
    // texel (measured, gp0.c buildSpans), so what its pixel shows is put around the block's centre; the
    // copy evaluates there, half a 1x pixel up and left of its own pixel
    // centre. A 1:1 map (2D) gives each texel an exact N x N block.
    // Projected 3D faces instead sample at the actual covered position.
    // A native-block shift can cross a thin face or a quad's diagonal,
    // selecting another texture strip as the face turns. Keep sprite,
    // HUD, text and integer-fallback sampling on the native block grid.
    float shift = (vMode & DRAW_PRECISE) != 0u ? 0.0 : (0.5 - 0.5 * float(n)) / step;
    vec2 uvDx = dFdx(vUV), uvDy = dFdy(vUV);
    vec2 uvAt = vUV + shift * (uvDx + uvDy) + 0.5;
    vec3 colorAt = vColor + shift * (dFdx(vColor) + dFdy(vColor));
    bool spanInside = true;
    if ((evolving || cachedCapture) && n == 1) {
        uint first = vSample.y;
        int row = q.y-int(pixels[first]);
        spanInside = vSample.z != 0u && row >= 0 && row < int(pixels[first+1u]);
        if (spanInside) {
            uint r = first+8u+uint(row)*8u;
            int k = q.x-int(pixels[r]);
            spanInside = k >= 0 && q.x <= int(pixels[r+1u]);
            uvAt = vec2(int(pixels[r+2u]+uint(k)*pixels[first+2u])>>16,
                        int(pixels[r+3u]+uint(k)*pixels[first+3u])>>16);
        }
    }
    uint mode = vMode;
#ifdef TILE_IMAGE
    // Derivatives have already run. Covered seam fragments can leave both
    // color and depth untouched before the texture and palette lookups.
    bool checkDestination = (mode & (DRAW_FRINGE | DRAW_CHECK_MASK)) != 0u;
    uint checkedPixel = 0u;
    if (checkDestination) {
        checkedPixel = colorAttachmentReadEXT(tilePixel).r;
        if (!hiKeeps(checkedPixel, mode)) discard;
    }
#endif
    bool resolved=(mode&DRAW_RESOLVED)!=0u;
    uint resolvedWord=0u;
    if(resolved) {
        ivec2 origin=ivec2(int(vSample.y&65535u),int(vSample.y>>16));
        uint at=uint(q.y-origin.y)*vSample.z+uint(q.x-origin.x);
        uint packed=pixels[vSample.x+at/2u];
        resolvedWord=word15((packed>>((at&1u)*16u))&65535u);
    }
    uint semiMode = (mode >> 3) & 3u;
    bool write = spanInside && !replayed;
    bool semi = (mode & DRAW_SEMI) != 0u;
    bool dithered = (mode & DRAW_DITHER) != 0u;
    bool raw = pc.color == 3; // 8-bit fronts everywhere
    uint maskOut = (mode & DRAW_SET_MASK) != 0u ? MASK : 0u;
    ivec3 c8 = clamp(ivec3(floor(colorAt + 0.5)), ivec3(0), ivec3(255));
    ivec3 front8 = ivec3(0);  // the dithered paths: 8-bit (may exceed 255)
    ivec3 front5 = ivec3(0);  // the others: 5-bit (may exceed 31), as the PS1 has it
    uint fill = 0u;

    if (resolved || (mode & DRAW_FILL) != 0u) {
        fill = resolved ? resolvedWord&0xffffffu : pack8(expand5(c8 >> 3));
        semi = false;
        dithered = false;
        maskOut = resolved ? resolvedWord&MASK : 0u;
    } else {
        ivec2 cmin = ivec2(vClipMin & 0xFFFFu, vClipMin >> 16);
        ivec2 cmax = ivec2(vClipMax & 0xFFFFu, vClipMax >> 16);
        if (any(lessThan(q, cmin)) || any(greaterThan(q, cmax))) {
#ifdef TILE_IMAGE
            discard;
#else
            write = false;
#endif
        }
        if ((mode & DRAW_TEXTURED) != 0u) {
            // The texel in hi-res texel units (texel * N + sub-texel).
            ivec2 h = ivec2(floor(uvAt * float(n)));
            ivec2 lo = ivec2(int(vUVMin << 16) >> 16, int(vUVMin) >> 16);
            ivec2 hi = ivec2(int(vUVMax << 16) >> 16, int(vUVMax) >> 16);
            h = clamp(h, lo * n, hi * n + (n - 1));
            ivec2 t = floorDiv(h, n);
            ivec2 sub = h - t * n;
            ivec2 uv = t & 0xFF;
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
            uint word;
            bool clear;
            if ((vSample.w & 0x80000u) != 0u) {
                // Owned frozen capture, or the ordered private framebuffer.
                ivec2 offset = ivec2(int(vSample.x << 16) >> 16, int(vSample.x) >> 16);
                ivec2 at = uv*n + sub + offset*n;
                if((vSample.w&0x10000u)!=0u)word=imageLoad(replayCache,at).r;
                else word = all(greaterThanEqual(at,ivec2(0))) && all(lessThan(at,imageSize(DST))) ? (evolving && (vSample.w&0x800000u)==0u ? imageLoad(DST,at).r : imageLoad(REPLAY_SOURCE,at).r) : 0u;
                clear = (word & (MASK | 0xF8F8F8u)) == 0u;
            } else if ((vSample.w & 16u) != 0u && (vSample.w & 255u) >= 17u) {
                // The display-copy sample stores filter + 1 in its low byte.
                word = anisotropicPixel(uvAt, uvDx, uvDy, lo, hi, (vSample.w & 255u) - 1u, clear);
            } else if (vSample.w != 0u) {
                word = filteredPixel(uvAt,lo,hi,clear);
            } else if ((tpage & ~0x60u) == 0x8180u) {
                uint c = promptTexel(clut,uint(uv.x),uint(uv.y));
                clear = c == 0u;
                word = word15(c);
            } else if (depth == 0u) {
                uint w = fetch(tx + (uv.x >> 2), ty + uv.y);
                uint c = fetch(cx + int((w >> ((uv.x & 3) * 4)) & 0xFu), cy);
                clear = c == 0u;
                word = word15(c);
            } else if (depth == 1u) {
                uint w = fetch(tx + (uv.x >> 1), ty + uv.y);
                uint c = fetch(cx + int((w >> ((uv.x & 1) * 8)) & 0xFFu), cy);
                clear = c == 0u;
                word = word15(c);
            } else {
                ivec2 a = ivec2(((tx + uv.x) & 1023) * n + sub.x, ((ty + uv.y) & 511) * n + sub.y);
                // As draw.frag: inside the 4:3 draw area a texel past VRAM's
                // right edge wraps; beside it the wide copy goes on. The VRAM
                // copy of a command the wide pass drew reads the same wide
                // texels (gp0.c hiVertex), so both copies come out the same.
                bool beside = q.x < int(vWideEdge & 0xFFFFu) || q.x > int(vWideEdge >> 16);
                if (vWideTex != 0u && (tx + uv.x < 1024 || beside)) {
                    int dx = int(vWideTex << 16) >> 16;
                    int dy = int(vWideTex << 1) >> 17;
                    ivec2 w = ivec2(((tx + uv.x + dx) & 2047) * n + sub.x, ((ty + uv.y + dy) & 2047) * n + sub.y);
                    word = all(lessThan(w, pc.wideSize)) ? imageLoad(hiWide, w).r : 0u;
                } else {
                    word = imageLoad(hiVram, a).r;
                }
                // Transparent as the 1x texel would be: 0000h.
                clear = (word & (MASK | 0xF8F8F8u)) == 0u;
            }
            if (clear) {
                write = false;
            }
            if ((word & MASK) == 0u && (mode & DRAW_FADE) == 0u) {
                semi = false;
            }
            maskOut |= word & MASK;
            ivec3 m = (mode & DRAW_RAW) != 0u ? ivec3(128) : c8;
            ivec3 t8 = rgb8(word);
            ivec3 t5 = t8 >> 3;
            if (dithered || raw) {
                front8 = pc.color == 0 || raw ? (t8 * m) >> 7 : (t5 * m) >> 4;
            } else {
                front5 = (t5 * m) >> 7;
            }
        } else if (dithered || raw) {
            front8 = c8;
        } else {
            front5 = c8 >> 3;
        }
    }

    front5 = clamp(front5, 0, 31);
    front8 = clamp(front8, 0, 255);
    uint emission = 0u;
    if ((mode & DRAW_HDR_EMISSION) != 0u && semi && (semiMode == 1u || semiMode == 3u)) {
        float peak = hdrPeak(vec3(front8)) / 255.0;
        emission = hdrEmission(smoothstep(0.35, 1.0, peak) * (semiMode == 3u ? 0.25 : 1.0));
    }
#ifdef ORDERED_LIST
    if(write) {
        uint flags=orderedFlags(resolved?mode|DRAW_FILL:mode,semi,semiMode,maskOut!=0u),front;
#ifdef ORDERED_MSAA
        flags |= (uint(gl_SampleMaskIn[0]) & 15u) << ORD_SAMPLE_SHIFT;
#endif
        flags |= (emission >> HDR_SHIFT) << ORD_HDR_SHIFT;
        if(textCover<1.0)flags|=uint(clamp(floor(textCover*255.0+0.5),1.0,254.0))<<ORD_COVER_SHIFT;
        // Collect the footprint on the display grid, where each pixel has
        // its own destination colour, mask, dither phase and depth.
        for (int by = 0; by < extent.y; by++) for (int bx = 0; bx < extent.x; bx++) {
            ivec2 pp = p + ivec2(bx, by);
            uint pixelFlags = flags;
            if(resolved || (mode&DRAW_FILL)!=0u)front=fill;
            else if(raw)front=pack8(front8);
            else if(pc.color==0&&dithered)front=pack8(ditherAverage(front8));
            else if(pc.color==0){front=pack8(expand5(front5));pixelFlags|=ORD_QUARTER5;}
            else {
                ivec3 f=front5;
                if(dithered) {
                    ivec2 dp=pc.color==1?q+pc.ditherOrigin:pp+pc.ditherOrigin*n;
                    f=clamp((front8+dither[(dp.y&3)*4+(dp.x&3)])>>3,0,31);
                }
                front=uint(f.r)|uint(f.g)<<5|uint(f.b)<<10;pixelFlags|=ORD_QUANT5;
            }
#ifdef DEPTH
            // The interlocked pass's rule (below): a fragment that replaces the
            // pixel owns its depth; ordered.comp keeps the last one that does.
            ivec2 zp = pp - pc.depthOrigin;
            uint z = 0u;
            if (vInvZ != 0.0 && !semi && textCover >= 1.0 &&
                all(greaterThanEqual(zp, ivec2(0))) && all(lessThan(zp, pc.depthSize))) {
                z = depthWord();
                pixelFlags |= ORD_DEPTH;
            }
            orderedAppend(pp,front,pixelFlags,z);
#else
            orderedAppend(pp,front,pixelFlags,0u);
#endif
        }
    }
#elif defined(TILE_IMAGE)
    // As draw.frag's: a fragment that writes nothing leaves the attachment
    // alone, and tile memory is read only by the fragments that are kept.
    if (write) {
        uint dst = checkDestination ? checkedPixel :
                   (semi || textCover < 1.0 ? colorAttachmentReadEXT(tilePixel).r : 0u);
        outPixel = hiPixel(dst, p, q, mode, resolved, fill, raw, semi, semiMode, dithered, front8, front5, maskOut, emission);
#ifdef DEPTH
        outDepth = vInvZ != 0.0 && !semi && textCover >= 1.0 ? depthWord() : colorAttachmentReadEXT(tileDepth).r;
#endif
    } else {
        discard;
    }
#else
    BEGIN_ORDERED();
    if (write) for (int by = 0; by < extent.y; by++) for (int bx = 0; bx < extent.x; bx++) {
        ivec2 pp = p + ivec2(bx, by); // the block's pixel (p itself at full resolution)
        uint dst = imageLoad(DST, pp).r;
        bool ownsDepth = hiKeeps(dst, mode);
#ifdef MSAA
        uint allSamples = (1u << sampleCount) - 1u;
        uint coverageMask = uint(gl_SampleMaskIn[0]) & allSamples;
        ivec2 sp = pp + vRasterShift - ivec2(pc.sampleX, pc.sampleY);
        uint point = uint(sp.y * pc.sampleStride + sp.x);
        uint at = point * sampleCount;
        bool split = sampleStamps[point] == pc.sampleEpoch;

        // An opaque surface covering every sample replaces the whole pixel.
        // Uniform pixels can otherwise take the usual path when fully covered.
        bool opaqueSurface = !semi && textCover >= 1.0 &&
            (mode & (DRAW_SURFACE | DRAW_FRINGE | DRAW_CHECK_MASK)) == DRAW_SURFACE;
        if (coverageMask == allSamples && (!split || opaqueSurface)) {
            if (split) sampleStamps[point] = 0u;
            if (opaqueSurface) ownsDepth = true;
            if (ownsDepth) {
                imageStore(DST, pp, uvec4(hiPixel(dst, pp, q, mode, resolved, fill, raw, semi, semiMode,
                                                  dithered, front8, front5, maskOut, emission)));
            }
        } else {
            ivec3 sum = ivec3(0);
            uint energy = 0u, flags = 0u;
            for (uint s = 0u; s < sampleCount; ++s) {
                uint word = split ? sceneSamples[at + s] : dst;
                bool replace = (coverageMask & (1u << s)) != 0u && hiKeeps(word, mode);
                if (replace)
                    word = hiPixel(word, pp, q, mode, resolved, fill, raw, semi, semiMode,
                                   dithered, front8, front5, maskOut, emission);
                sceneSamples[at + s] = word;
                sum += rgb8(word);
                energy += word >> HDR_SHIFT;
                // Mask, seam coverage and scene depth are categorical.
                // Use the same reference sample for all three at resolve.
                if (s == 0u) {
                    flags = word & (MASK | COVERED);
                    ownsDepth = replace;
                }
            }
            sampleStamps[point] = pc.sampleEpoch;
            uint hdr = ((energy + sampleCount / 2u) / sampleCount) << HDR_SHIFT;
            imageStore(DST, pp, uvec4(pack8((sum + int(sampleCount / 2u)) / int(sampleCount)) | flags | hdr));
        }
#else
        if (ownsDepth) {
            imageStore(DST, pp, uvec4(hiPixel(dst, pp, q, mode, resolved, fill, raw, semi, semiMode, dithered,
                                              front8, front5, maskOut, emission)));
        }
#endif
#ifdef DEPTH
        // A fragment that replaces the pixel owns its depth; one that
        // blends (semi) or only smooths a text edge leaves it.
        ivec2 dp = pp - pc.depthOrigin;
        if (ownsDepth && vInvZ != 0.0 && !semi && textCover >= 1.0 &&
            all(greaterThanEqual(dp, ivec2(0))) && all(lessThan(dp, pc.depthSize))) {
            imageStore(sceneDepth, dp, uvec4(depthWord()));
        }
#endif
    }
    END_ORDERED();
#endif
}
