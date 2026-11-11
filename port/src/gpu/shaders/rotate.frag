#version 450

// Pre-rotation (vk.c, xg-mrj.3.9): the picture as the player sees it (the
// present pass's image, the window's size and orientation) into a swapchain
// image of the display's own orientation, turned `quarter` quarter turns
// clockwise, texel for texel (the two have the same pixels, turned).

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Turn {
    int quarter;
} turn;

layout(location = 0) out vec4 color;

void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(picture, 0);
    ivec2 from = at;
    if (turn.quarter == 1) {
        from = ivec2(at.y, size.y - 1 - at.x);
    } else if (turn.quarter == 2) {
        from = size - 1 - at;
    } else if (turn.quarter == 3) {
        from = ivec2(size.x - 1 - at.y, at.x);
    }
    color = texelFetch(picture, clamp(from, ivec2(0), size - 1), 0);
}
