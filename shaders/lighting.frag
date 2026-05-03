#version 410 core
#include "common.glsl"

uniform sampler2D normalRoughness, baseMetallic, emissionAO, depthMap, reflectionMap, giMap;
uniform bool useReflections, useGI;

in vec2 uv;
out vec4 color;

void main() {
    float depth = texture(depthMap, uv).r;

    if (depth >= 1.) {
        vec3 direction = normalize(worldPosition(uv, .99999) - cameraPosition);
        color = vec4(texture(environmentMap, sphereUV(direction)).rgb * environmentIntensity, 1);
        return;
    }

    vec4 n = texture(normalRoughness, uv), b = texture(baseMetallic, uv),
         e = texture(emissionAO, uv);
    Surface s;
    s.normal = n.xyz;
    s.roughness = n.w;
    s.base = b.rgb;
    s.metallic = b.w;
    s.emissive = e.rgb;
    s.ao = e.w;
    s.alpha = 1.;
    vec3 p = worldPosition(uv, depth), v = normalize(cameraPosition - p);
    vec3 diffuse = useGI ? texture(giMap, uv).rgb : irradiance(s.normal) / PI;
    vec3 spec = useReflections ? texture(reflectionMap, uv).rgb : specularIBL(s, v);
    color = vec4(directLighting(s, p, v) + diffuseWeight(s, v) * s.base * diffuse * s.ao + spec +
                     s.emissive,
                 1);
}
