#version 410 core
#include "common.glsl"

in vec3 worldNormal, worldPos;
in vec4 worldTangent, currentClip, previousClip;
in vec2 uv;

uniform int materialIndex;
out vec4 color;

void main() {
    vec3 n = normalize(gl_FrontFacing ? worldNormal : -worldNormal);
    vec2 dx = dFdx(uv) * vec2(textureSize(baseMaps, 0).xy),
         dy = dFdy(uv) * vec2(textureSize(baseMaps, 0).xy);
    Surface s = surfaceAt(materialIndex, uv, n, worldTangent,
                          max(0., .5 * log2(max(dot(dx, dx), dot(dy, dy)))));
    vec3 v = normalize(cameraPosition - worldPos);
    color = vec4(directLighting(s, worldPos, v) +
                     diffuseWeight(s, v) * s.base * irradiance(s.normal) / PI * s.ao +
                     specularIBL(s, v) + s.emissive,
                 s.alpha);
}
