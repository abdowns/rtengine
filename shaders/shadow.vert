#version 410 core

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;

uniform mat4 model, shadowMatrix;
out vec2 uv;

void main() {
    uv = inUV;
    gl_Position = shadowMatrix * model * vec4(inPosition, 1);
}
