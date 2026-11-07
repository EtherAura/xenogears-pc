#version 450
// The extras' views (view.cpp): what the PS1 GPU does per pixel, in 8-bit
// colour (psx-spx "GPU Rendering Attributes", "GPU Texture Caching"):
// texture pages and CLUTs read from the view's VRAM, the texture window,
// texel 0 clear, modulation by the vertex colour (/128), and the
// semi-transparent texels (STP) of semi-transparent primitives blended by the
// pipeline in a second pass (pass 1), the rest drawn opaque (pass 0).
// The Enhanced look (view.cpp, replace.h): a replaced primitive reads its
// pack picture instead, at its scale over the native rectangle it stands
// for: an index map through the primitive's CLUT (as the native texels), or
// RGBA (alpha 0 clear, 128 STP).

layout(set = 0, binding = 0) uniform usampler2D vram;
layout(set = 1, binding = 0) uniform usampler2D repIndex;
layout(set = 1, binding = 1) uniform sampler2D repRgba;

layout(push_constant) uniform Push {
    vec2 scale;
    vec2 offset;
    uint flags;  // 1: keep primitives inside their draw area, 2: textures on, 4: lines (wireframe)
    uint pass;   // 0 opaque, 1 semi-transparent texels
} pc;

layout(location = 0) noperspective in vec2 uv;
layout(location = 1) noperspective in vec4 color;
layout(location = 2) flat in uvec4 tex;
layout(location = 3) flat in uint mode;
layout(location = 4) flat in ivec4 area;
layout(location = 5) noperspective in vec2 scenePos;
layout(location = 6) flat in ivec4 rep;

layout(location = 0) out vec4 outColor;

// view.cpp MODE_*
const uint DEPTH = 3u;      // 0 4-bit, 1 8-bit, 2 15-bit
const uint TEXTURED = 4u;
const uint RAW = 8u;
const uint SEMI = 16u;
const uint FREE_UV = 128u;  // u, v from the page corner, no 8-bit wrap or window

uint fetch(int x, int y) {
    return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r;
}

void main() {
    if ((pc.flags & 1u) != 0u &&
        (scenePos.x < float(area.x) || scenePos.y < float(area.y) || scenePos.x >= float(area.z + 1) ||
         scenePos.y >= float(area.w + 1))) {
        discard;
    }
    bool semi = (mode & SEMI) != 0u;
    bool stp = semi; // untextured: the whole primitive is blended
    vec3 rgb = color.rgb;
    if ((mode & TEXTURED) != 0u && (pc.flags & 2u) != 0u && rep.w != 0) {
        ivec2 size = rep.w == 1 ? textureSize(repIndex, 0) : textureSize(repRgba, 0);
        ivec2 t = clamp(ivec2(floor((uv - vec2(rep.xy)) * float(rep.z))), ivec2(0), size - 1);
        uint texel;
        vec3 c;
        if (rep.w == 1) {
            texel = fetch(int(tex.z) + int(texelFetch(repIndex, t, 0).r), int(tex.w));
            if (texel == 0u) {
                discard;
            }
            stp = semi && (texel & 0x8000u) != 0u;
            uvec3 c5 = uvec3(texel & 31u, (texel >> 5) & 31u, (texel >> 10) & 31u);
            c = vec3((c5 << 3u) | (c5 >> 2u)) / 255.0;
        } else {
            vec4 p = texelFetch(repRgba, t, 0);
            if (p.a < 0.25) {
                discard;
            }
            stp = semi && p.a < 0.75;
            c = p.rgb;
        }
        rgb = (mode & RAW) != 0u ? c : min(c * color.rgb * (255.0 / 128.0), vec3(1.0));
    } else if ((mode & TEXTURED) != 0u && (pc.flags & 2u) != 0u) {
        ivec2 t = ivec2(floor(uv));
        if ((mode & FREE_UV) == 0u) {
            uint w = (mode >> 8) & 0xFFFFFu;
            uint mx = w & 31u, my = (w >> 5) & 31u, ox = (w >> 10) & 31u, oy = (w >> 15) & 31u;
            t &= 255;
            t.x = int((uint(t.x) & ~(mx * 8u)) | ((ox & mx) * 8u));
            t.y = int((uint(t.y) & ~(my * 8u)) | ((oy & my) * 8u));
        }
        uint depth = mode & DEPTH;
        uint texel;
        if (depth == 0u) {
            uint w = fetch(int(tex.x) + (t.x >> 2), int(tex.y) + t.y);
            texel = fetch(int(tex.z) + int((w >> (uint(t.x & 3) * 4u)) & 15u), int(tex.w));
        } else if (depth == 1u) {
            uint w = fetch(int(tex.x) + (t.x >> 1), int(tex.y) + t.y);
            texel = fetch(int(tex.z) + int((w >> (uint(t.x & 1) * 8u)) & 255u), int(tex.w));
        } else {
            texel = fetch(int(tex.x) + t.x, int(tex.y) + t.y);
        }
        if (texel == 0u) {
            discard;
        }
        stp = semi && (texel & 0x8000u) != 0u;
        // 5 bits to 8 as the exports widen them (c << 3 | c >> 2).
        uvec3 c5 = uvec3(texel & 31u, (texel >> 5) & 31u, (texel >> 10) & 31u);
        vec3 c = vec3((c5 << 3u) | (c5 >> 2u)) / 255.0;
        rgb = (mode & RAW) != 0u ? c : min(c * color.rgb * (255.0 / 128.0), vec3(1.0));
    } else if ((mode & TEXTURED) != 0u) {
        // Textures off: the colour a white texel would have.
        rgb = min(color.rgb * (255.0 / 128.0), vec3(1.0));
        stp = false;
    }
    if ((pc.pass == 0u && stp) || (pc.pass == 1u && !stp)) {
        discard;
    }
    uint abr = (mode >> 5) & 3u;
    float a = pc.pass == 1u ? (abr == 0u ? 0.5 : abr == 3u ? 0.25 : 1.0) : 1.0;
    outColor = vec4(rgb, a);
}
