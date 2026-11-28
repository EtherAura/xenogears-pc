#version 450

// The hi-res copy's triangles (port/src/gpu/hires.c, gp0.c): positions in 1x
// VRAM (or wide target) coordinates, drawn N times larger. Built with WIDE:
// for the hi-res wide target's pass (it also passes the wide fields on).
//
// The sample grid: hi-res pixel p (in a block of N x N per 1x pixel) samples
// the primitive at 1x position p / N, so sub-pixel 0 of each block sits on
// the PS1's own sample point. Vulkan samples a pixel at its centre (p + 0.5),
// so positions move half a hi-res pixel right and down: an edge at integer
// 1x x = X then falls exactly between blocks, and the left/top edges are
// inclusive and the right/bottom ones exclusive, as on the PS1.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inColor;
layout(location = 3) in uint inMode;
layout(location = 4) in uint inTex;
layout(location = 5) in uint inTexWindow;
layout(location = 6) in uint inClipMin;
layout(location = 7) in uint inClipMax;
layout(location = 8) in uint inUVMin;
layout(location = 9) in uint inUVMax;
layout(location = 10) in uint inWideTex;
layout(location = 11) in uint inWideEdge;
layout(location = 12) in float inW;
layout(location = 13) in uvec4 inSample;
layout(location = 14) in uint inRasterShift;
#ifdef DEPTH
// The scene's depth image (scene.c): the view depth at the vertex, negated
// for a figure (a sprite), 0 for none, 1e30 for a picture kept in focus
// (HiVertex.z).
layout(location = 15) in float inZ;
#endif

layout(push_constant) uniform Hi {
    int n;          // N
    int color;      // 0 true colour, 1 15-bit dithered at 1x size, 2 at hi-res size
    ivec2 target;   // the target's size in hi-res pixels
    ivec2 wideSize; // the hi-res wide target's size (texel reads stay inside)
    ivec2 ditherOrigin; // private replay's origin in the authored target, at 1x
    int rasterScale; // samples per native pixel in the COARSE build
} pc;

#ifdef DEPTH
// 1 / view depth is linear on the screen, so it is carried without the
// perspective division and is exact across a flat face whether the face's
// textures are perspective-correct or not; negative for a figure. A marker
// is one value for the whole triangle (the three vertices agree, gp0.c): 0
// none, 2 a picture kept in focus (a depth is at least 1, so 1 / z is not).
layout(location = 14) noperspective out float vInvZ;
#endif

layout(location = 0) out vec2 vUV;
layout(location = 1) noperspective out vec3 vColor;
layout(location = 2) flat out uint vMode;
layout(location = 3) flat out uint vTex;
layout(location = 4) flat out uint vTexWindow;
layout(location = 5) flat out uint vClipMin;
layout(location = 6) flat out uint vClipMax;
layout(location = 7) flat out uint vUVMin;
layout(location = 8) flat out uint vUVMax;
layout(location = 11) flat out uvec4 vSample;
layout(location = 13) flat out ivec2 vRasterShift; // hi-res pixels (HiVertex.rasterShift)
layout(location = 9) flat out uint vWideTex;
layout(location = 10) flat out uint vWideEdge;

#ifdef ORDERED_LIST
layout(location = 12) flat out uint vOrder;
#endif

void main() {
#ifdef ORDERED_LIST
    vOrder = uint(gl_VertexIndex) / 3u;
#endif
    // The VRAM copy's triangles rasterise where the wide pass's do (the shift
    // added as the wide pass added it, exactly: gp0.c preciseSnap) and the
    // fragment stores them back (HiVertex.rasterShift); pc.target is one
    // viewport for both passes (vk.c drawHiBatch). Identical inputs, so the
    // device gives both copies the same coverage, texels and shades.
    ivec2 shift = ivec2(int(inRasterShift << 16) >> 16, int(inRasterShift) >> 16);
    vRasterShift = shift * pc.n;
#ifdef COARSE
    vec2 p = (inPos + vec2(shift)) * float(pc.rasterScale) + 0.5;
#else
    vec2 p = (inPos + vec2(shift)) * float(pc.n) + 0.5;
#endif
    gl_Position = vec4((p / vec2(pc.target) * 2.0 - 1.0) * inW, 0.0, inW);
    vUV = inUV;
    vColor = inColor;
    vMode = inMode;
    vTex = inTex;
    vTexWindow = inTexWindow;
    vClipMin = inClipMin;
    vClipMax = inClipMax;
    vUVMin = inUVMin;
    vUVMax = inUVMax;
    vSample = inSample;
    vWideTex = inWideTex;
    vWideEdge = inWideEdge;
#ifdef DEPTH
    vInvZ = inZ == 0.0 ? 0.0 : inZ >= 1.0e29 ? 2.0 : 1.0 / inZ;
#endif
}
