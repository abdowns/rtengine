#version 410 core

uniform sampler2D hdrMap;
uniform float exposure;
uniform bool useFXAA;

in vec2 uv;
out vec4 color;

vec3 mapped(vec2 p) {
    vec3 c = texture(hdrMap, p).rgb * exposure;
    c = clamp((c * (2.51 * c + .03)) / (c * (2.43 * c + .59) + .14), 0., 1.);
    return pow(c, vec3(1. / 2.2));
}

float luma(vec3 c) {
    return dot(c, vec3(.299, .587, .114));
}

void main() {
    vec3 c = mapped(uv);

    if (useFXAA) {
        vec2 px = 1. / vec2(textureSize(hdrMap, 0));
        vec3 nw = mapped(uv + vec2(-1, -1) * px), ne = mapped(uv + vec2(1, -1) * px),
             sw = mapped(uv + vec2(-1, 1) * px), se = mapped(uv + vec2(1, 1) * px);
        float lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se), lc = luma(c);
        vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
        float reduce = max((lnw + lne + lsw + lse) * .03125, .0078125);
        dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8), vec2(8)) * px;
        vec3 a = .5 * (mapped(uv + dir * (1. / 3. - .5)) + mapped(uv + dir * (2. / 3. - .5))),
             b = a * .5 + .25 * (mapped(uv - dir * .5) + mapped(uv + dir * .5));
        float lb = luma(b);
        c = (lb < min(lc, min(min(lnw, lne), min(lsw, lse))) ||
             lb > max(lc, max(max(lnw, lne), max(lsw, lse))))
                ? a
                : b;
    }

    color = vec4(c, 1);
}
