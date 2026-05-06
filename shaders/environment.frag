#version 410 core

const float PI = 3.14159265359;
uniform sampler2D sourceMap;
uniform int mode;
uniform float roughness;

in vec2 uv;
out vec4 color;

vec3 direction(vec2 coord) {
    float phi = (coord.x - .5) * 2. * PI, theta = coord.y * PI;
    return vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}

vec2 sphereUV(vec3 d) {
    return vec2(atan(d.z, d.x) / (2. * PI) + .5, acos(clamp(d.y, -1., 1.)) / PI);
}

float radical(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec3 sampleGGX(vec2 xi, float r) {
    float a = r * r, phi = 2. * PI * xi.x, c = sqrt((1. - xi.y) / (1. + (a * a - 1.) * xi.y));
    return vec3(sqrt(1. - c * c) * cos(phi), sqrt(1. - c * c) * sin(phi), c);
}

void main() {
    if (mode == 2) {
        float nv = max(uv.x, .001), r = uv.y;
        vec3 v = vec3(sqrt(1. - nv * nv), 0, nv);
        vec2 sum = vec2(0);

        for (uint i = 0u; i < 128u; ++i) {
            vec3 h = sampleGGX(vec2(float(i) / 128., radical(i)), r),
                 l = normalize(2. * dot(v, h) * h - v);
            float nl = max(l.z, 0.), nh = max(h.z, 0.), vh = max(dot(v, h), 0.);

            if (nl > 0.) {
                float k = r * r * .5, g = (nv / (nv * (1. - k) + k)) * (nl / (nl * (1. - k) + k));
                float vis = g * vh / max(nh * nv, 1e-5), fc = pow(1. - vh, 5.);
                sum += vec2((1. - fc) * vis, fc * vis);
            }
        }

        color = vec4(sum / 128., 0, 1);
        return;
    }

    vec3 n = direction(uv), t = normalize(cross(abs(n.y) < .99 ? vec3(0, 1, 0) : vec3(1, 0, 0), n));
    mat3 basis = mat3(t, cross(n, t), n);
    vec3 sum = vec3(0);
    float weight = 0.;

    for (uint i = 0u; i < 128u; ++i) {
        vec2 xi = vec2(float(i) / 128., radical(i));
        vec3 d;

        if (mode == 0) {
            float r = sqrt(xi.y), a = 2. * PI * xi.x;
            d = basis * vec3(r * cos(a), r * sin(a), sqrt(1. - xi.y));
            sum += textureLod(sourceMap, sphereUV(d), 0.).rgb;
            weight += 1.;
        } else {
            vec3 h = basis * sampleGGX(xi, max(roughness, .001));
            d = reflect(-n, h);
            float w = max(dot(n, d), 0.);
            sum += textureLod(sourceMap, sphereUV(d), roughness * 4.).rgb * w;
            weight += w;
        }
    }

    color = vec4(sum / max(weight, 1e-5) * (mode == 0 ? PI : 1.), 1);
}
