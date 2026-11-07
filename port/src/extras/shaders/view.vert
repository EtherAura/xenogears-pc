#version 450
// The extras' views (view.cpp): PS1 primitives at any size. Positions are in
// the scene's units (VRAM / screen pixels); the push constants map them to
// the target. Colours and texture coordinates are affine, as on the PS1.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;  // 8-bit, 0-255 as 0-1
layout(location = 3) in uvec4 inTex;   // page x, page y, CLUT x, CLUT y (VRAM)
layout(location = 4) in uint inMode;   // view.cpp MODE_*
layout(location = 5) in ivec4 inArea;  // draw area x0, y0, x1, y1 (inclusive)
layout(location = 6) in ivec4 inRep;   // a replacement: its u, v, scale, kind (0 none, 1 index map, 2 RGBA)

layout(push_constant) uniform Push {
    vec2 scale;
    vec2 offset;
    uint flags;
    uint pass;
} pc;

layout(location = 0) noperspective out vec2 uv;
layout(location = 1) noperspective out vec4 color;
layout(location = 2) flat out uvec4 tex;
layout(location = 3) flat out uint mode;
layout(location = 4) flat out ivec4 area;
layout(location = 5) noperspective out vec2 scenePos;
layout(location = 6) flat out ivec4 rep;

void main() {
    gl_Position = vec4(inPos * pc.scale + pc.offset, 0.0, 1.0);
    uv = inUv;
    color = inColor;
    tex = inTex;
    mode = inMode;
    area = inArea;
    scenePos = inPos;
    rep = inRep;
}
