#version 450
// Each picture pixel as a 2 x 2 block (nearest, scale 2 in the preset).
#pragma name Doubled
#include "common.inc"

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
    FragColor = texture(Source, vTexCoord);
}
