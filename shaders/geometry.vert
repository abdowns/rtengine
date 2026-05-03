#version 410 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

uniform mat4 model, previousModel, viewProjection, previousViewProjection;
out vec3 worldNormal, worldPos;
out vec4 worldTangent, currentClip, previousClip;
out vec2 uv;

void main() {
    vec4 p = model * vec4(inPosition, 1);
    worldPos = p.xyz;
    worldNormal = normalize(transpose(inverse(mat3(model))) * inNormal);
    worldTangent = vec4(normalize(mat3(model) * inTangent.xyz),
                        inTangent.w * (determinant(mat3(model)) < 0. ? -1. : 1.));
    uv = inUV;
    currentClip = viewProjection * p;
    previousClip = previousViewProjection * previousModel * vec4(inPosition, 1);
    gl_Position = currentClip;
}
