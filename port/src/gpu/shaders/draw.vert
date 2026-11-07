#version 450

// Triangles into VRAM: positions are VRAM pixel coordinates (port/src/gpu/gp0.c).
// Built with WIDE: into the wide target (wide.c), positions in its coordinates.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inColor;
layout(location = 3) in uint inMode;
layout(location = 4) in uint inTex;
layout(location = 5) in uint inTexWindow;
layout(location = 6) in uint inClipMin;
layout(location = 7) in uint inClipMax;
layout(location = 8) in uint inSpan;
layout(location = 9) in uint inWideTex;
layout(location = 10) in uint inWideEdge;

layout(location = 0) noperspective out vec2 vUV;
layout(location = 1) noperspective out vec3 vColor;
layout(location = 2) flat out uint vMode;
layout(location = 3) flat out uint vTex;
layout(location = 4) flat out uint vTexWindow;
layout(location = 5) flat out uint vClipMin;
layout(location = 6) flat out uint vClipMax;
layout(location = 7) flat out uint vSpan;
#ifdef WIDE
layout(location = 8) flat out uint vWideTex;
layout(location = 9) flat out uint vWideEdge;
#endif

#ifdef ORDERED_LIST
layout(location = 10) flat out uint vOrder;
#endif

void main() {
#ifdef ORDERED_LIST
    vOrder = uint(gl_VertexIndex) / 3u;
#endif
#ifdef WIDE
    gl_Position = vec4(inPos * vec2(2.0 / 2048.0, 2.0 / 2048.0) - 1.0, 0.0, 1.0);
#else
    gl_Position = vec4(inPos * vec2(2.0 / 1024.0, 2.0 / 512.0) - 1.0, 0.0, 1.0);
#endif
    vUV = inUV;
    vColor = inColor;
    vMode = inMode;
    vTex = inTex;
    vTexWindow = inTexWindow;
    vClipMin = inClipMin;
    vClipMax = inClipMax;
    vSpan = inSpan;
#ifdef WIDE
    vWideTex = inWideTex;
    vWideEdge = inWideEdge;
#endif
}
