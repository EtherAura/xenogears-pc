#version 450
// One pass: the picture's colours inverted.
#include "common.inc"

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
    FragColor = vec4(1.0 - texture(Source, vTexCoord).rgb, 1.0);
}
