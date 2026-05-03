#version 410 core
#include "common.glsl"

in vec3 worldNormal, worldPos;
in vec4 worldTangent, currentClip, previousClip;
in vec2 uv;

uniform int materialIndex;

layout(location = 0) out vec4 outNormalRoughness;
layout(location = 1) out vec4 outBaseMetallic;
layout(location = 2) out vec4 outEmissionAO;
layout(location = 3) out vec4 outMotion;

void main() {
    if (cutout(materialIndex, uv))
        discard;

    vec3 n = gl_FrontFacing ? worldNormal : -worldNormal;
    vec2 dx = dFdx(uv) * vec2(textureSize(baseMaps, 0).xy),
         dy = dFdy(uv) * vec2(textureSize(baseMaps, 0).xy);
    float lod = max(0., .5 * log2(max(dot(dx, dx), dot(dy, dy))));
    Surface s = surfaceAt(materialIndex, uv, normalize(n), worldTangent, lod);
    outNormalRoughness = vec4(s.normal, s.roughness);
    outBaseMetallic = vec4(s.base, s.metallic);
    outEmissionAO = vec4(s.emissive, s.ao);
    vec2 current = currentClip.xy / currentClip.w * .5 + .5,
         prev = previousClip.xy / previousClip.w * .5 + .5;
    outMotion = vec4(current - prev, previousClip.z / previousClip.w * .5 + .5,
                     previousClip.w > 0. ? 1. : 0.);
}
