#version 450
// Does not compile: the test's error case.
#include "common.inc"

#pragma stage fragment
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(undefinedThing, 1.0);
}
