#version 450
// The doubled picture times a LUT's colour and a parameter, its odd output
// rows darkened: Source, Original, a named pass, a LUT, push constants.
#pragma parameter TINT_STRENGTH "Tint strength" 1.0 0.0 1.0 0.25
#pragma parameter ROW_DARK "Odd rows" 0.5 0.0 1.0 0.1
layout(push_constant) uniform Push {
    vec4 SourceSize;
    vec4 DoubledSize;
    float TINT_STRENGTH;
    float ROW_DARK;
} params;

#include "common.inc"

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;
layout(set = 0, binding = 3) uniform sampler2D Original;
layout(set = 0, binding = 4) uniform sampler2D Doubled;
layout(set = 0, binding = 5) uniform sampler2D Tint;

void main() {
    vec3 c = texture(Doubled, vTexCoord).rgb;
    vec3 tint = texture(Tint, vec2(0.25, 0.5)).rgb;
    c = mix(c, c * tint, params.TINT_STRENGTH);
    float row = floor(vTexCoord.y * global.OutputSize.y);
    if (mod(row, 2.0) >= 1.0) {
        c *= 1.0 - params.ROW_DARK;
    }
    // Original and Source agree with the doubled picture (unused but bound)
    FragColor = vec4(c + 0.0 * (texture(Original, vTexCoord).rgb - texture(Source, vTexCoord).rgb), 1.0);
}
