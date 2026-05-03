#version 410 core
#include "common.glsl"

in vec2 uv;

uniform int materialIndex;

void main() {
    if (cutout(materialIndex, uv))
        discard;
}
