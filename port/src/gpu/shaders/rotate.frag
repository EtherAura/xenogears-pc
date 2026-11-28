#version 450

// Pre-rotation (vk.c, xg-mrj.3.9): the picture as the player sees it (the
// present pass's image, the window's size and orientation) into a swapchain
// image of the display's own orientation, turned `quarter` quarter turns
// clockwise, texel for texel. HDR output decodes the composed SDR image
// into linear scRGB, scaled to the game's paper white.

layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Turn {
    int quarter;
    float whiteLevel; // zero for SDR; otherwise paper white in scRGB units
    float highlightRange; // extra brightness for highlights, in paper-white units
} turn;

layout(location = 0) out vec4 color;

vec3 srgbToLinear(vec3 srgb) {
    return mix(pow((srgb + 0.055) / 1.055, vec3(2.4)), srgb / 12.92, lessThanEqual(srgb, vec3(0.04045)));
}

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
    if (turn.whiteLevel > 0.0) {
        // Give the bright cores room above white while keeping the dim
        // edges of a glow or beam at their reference brightness.
        float peak = max(color.r, max(color.g, color.b));
        float emission = (1.0 - color.a) * smoothstep(0.60, 0.92, peak);
        float gain = 1.0 + emission * turn.highlightRange;
        color = vec4(srgbToLinear(color.rgb) * (turn.whiteLevel * gain), 1.0);
    }
}
