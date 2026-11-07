#version 450
// An unsharp mask: the picture plus AMOUNT times its difference from the
// average of its four neighbours (a small example preset of our own).
#pragma parameter AMOUNT "Sharpness" 0.5 0.0 2.0 0.1
layout(push_constant) uniform Push {
    vec4 SourceSize;
    float AMOUNT;
} params;

#include "common.inc"

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
    vec2 d = params.SourceSize.zw;
    vec3 c = texture(Source, vTexCoord).rgb;
    vec3 n = texture(Source, vTexCoord + vec2(d.x, 0.0)).rgb + texture(Source, vTexCoord - vec2(d.x, 0.0)).rgb +
             texture(Source, vTexCoord + vec2(0.0, d.y)).rgb + texture(Source, vTexCoord - vec2(0.0, d.y)).rgb;
    FragColor = vec4(clamp(c + params.AMOUNT * (c - 0.25 * n), 0.0, 1.0), 1.0);
}
