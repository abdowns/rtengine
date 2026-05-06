#version 410 core

uniform sampler2D reflectionMap, giMap, normalRoughness, depthMap;
uniform float stepWidth;

in vec2 uv;

layout(location = 0) out vec4 reflection;
layout(location = 1) out vec4 gi;

void main() {
    vec3 n = texture(normalRoughness, uv).xyz;
    float depth = texture(depthMap, uv).r;
    vec2 texel = stepWidth / vec2(textureSize(reflectionMap, 0));
    vec3 r = vec3(0), g = vec3(0);
    float sum = 0.;

    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 p = uv + vec2(x, y) * texel;
            vec3 ns = texture(normalRoughness, p).xyz;
            float ds = texture(depthMap, p).r;
            float w = exp(-.3 * float(x * x + y * y)) * pow(max(dot(n, ns), 0.), 32.) *
                      exp(-abs(depth - ds) * 1500.);
            r += texture(reflectionMap, p).rgb * w;
            g += texture(giMap, p).rgb * w;
            sum += w;
        }

    reflection = vec4(r / max(sum, 1e-6), 1);
    gi = vec4(g / max(sum, 1e-6), 1);
}
