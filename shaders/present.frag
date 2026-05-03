#version 410 core

uniform sampler2D hdrMap;
uniform float exposure;

in vec2 uv;
out vec4 color;

void main() {
    vec3 c = texture(hdrMap, uv).rgb * exposure;
    c = clamp((c * (2.51 * c + .03)) / (c * (2.43 * c + .59) + .14), 0., 1.);
    color = vec4(pow(c, vec3(1. / 2.2)), 1);
}
