#version 410 core

uniform sampler2D currentReflection, currentGI, historyReflection, historyGI, currentNormal,
    currentDepth, motionMap, historyNormal, historyDepth;
uniform bool historyValid;
uniform float historyWeight;

in vec2 uv;

layout(location = 0) out vec4 reflection;
layout(location = 1) out vec4 gi;
layout(location = 2) out vec4 normal;
layout(location = 3) out float depth;

vec3 clampHistory(sampler2D current, vec3 old) {
    vec3 lo = vec3(1e20), hi = vec3(-1e20);
    vec2 texel = 1. / vec2(textureSize(current, 0));

    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec3 s = texture(current, uv + vec2(x, y) * texel).rgb;
            lo = min(lo, s);
            hi = max(hi, s);
        }

    return clamp(old, lo, hi);
}

void main() {
    normal = texture(currentNormal, uv);
    depth = texture(currentDepth, uv).r;
    vec4 motion = texture(motionMap, uv);
    vec2 prev = uv - motion.xy;
    bool valid = historyValid && motion.w > .5 && depth < 1. &&
                 all(greaterThanEqual(prev, vec2(0))) && all(lessThanEqual(prev, vec2(1)));
    float oldDepth = texture(historyDepth, prev).r;
    vec3 oldNormal = texture(historyNormal, prev).xyz;
    valid = valid && abs(oldDepth - motion.z) < .002 && dot(oldNormal, normal.xyz) > .9;
    // short history reduces indirect trails
    float weight = valid ? historyWeight : 0.;
    reflection =
        vec4(mix(texture(currentReflection, uv).rgb,
                 clampHistory(currentReflection, texture(historyReflection, prev).rgb), weight),
             1);
    gi = vec4(mix(texture(currentGI, uv).rgb, clampHistory(currentGI, texture(historyGI, prev).rgb),
                  weight),
              1);
}
