#version 450
// Half this frame's picture, half the pass's own last output (PassFeedback0),
// and OriginalHistory1 (the last frame's picture) in the alpha-free blue
// channel's place for the history test.
#pragma parameter USE_HISTORY "Show the last frame" 0.0 0.0 1.0 1.0
layout(push_constant) uniform Push {
    float USE_HISTORY;
} params;

#include "common.inc"

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;
layout(set = 0, binding = 3) uniform sampler2D PassFeedback0;
layout(set = 0, binding = 4) uniform sampler2D OriginalHistory1;

void main() {
    vec3 now = texture(Source, vTexCoord).rgb;
    vec3 last = texture(PassFeedback0, vTexCoord).rgb;
    vec3 c = params.USE_HISTORY > 0.5 ? texture(OriginalHistory1, vTexCoord).rgb : 0.5 * now + 0.5 * last;
    FragColor = vec4(c, 1.0);
}
