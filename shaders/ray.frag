#version 410 core
#include "common.glsl"

uniform samplerBuffer blasNodes, tlasNodes, triangles, instances, instanceOrder;
uniform sampler2D normalRoughness, baseMetallic, depthMap, reflectionHistory, giHistory;
uniform mat4 previousViewProjection;
uniform int instanceCount, frameIndex;
uniform bool useReflections, useGI;

in vec2 uv;

layout(location = 0) out vec4 reflection;
layout(location = 1) out vec4 gi;
uint rng;

float randomValue() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng) / 4294967296.;
}

mat3 basis(vec3 n) {
    vec3 t = normalize(cross(abs(n.y) < .99 ? vec3(0, 1, 0) : vec3(1, 0, 0), n));
    return mat3(t, cross(n, t), n);
}

bool boxHit(vec3 o, vec3 d, vec3 lo, vec3 hi, float limit) {
    float nearT = .0001, farT = limit;

    for (int axis = 0; axis < 3; ++axis) {
        if (abs(d[axis]) < 1e-10) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis])
                return false;
        } else {
            float a = (lo[axis] - o[axis]) / d[axis], b = (hi[axis] - o[axis]) / d[axis];
            nearT = max(nearT, min(a, b));
            farT = min(farT, max(a, b));
        }
    }

    return farT >= nearT;
}

struct TraceHit {
    float distance;
    int triangle;
    int instance;
    vec2 bary;
};

bool triangleHit(vec3 o, vec3 d, int index, inout TraceHit hit, int instance) {
    vec3 a = texelFetch(triangles, index * 9).xyz, b = texelFetch(triangles, index * 9 + 1).xyz,
         c = texelFetch(triangles, index * 9 + 2).xyz;
    vec3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    float det = dot(e1, p);

    if (abs(det) < 1e-9)
        return false;
    vec3 delta = o - a;
    float u = dot(delta, p) / det;

    if (u < 0. || u > 1.)
        return false;
    vec3 q = cross(delta, e1);
    float v = dot(d, q) / det, t = dot(e2, q) / det;

    if (v < 0. || u + v > 1. || t <= .0001 || t >= hit.distance)
        return false;
    vec3 weights = vec3(1. - u - v, u, v);
    vec2 coord = vec2(0);

    for (int k = 0; k < 3; ++k)
        coord += vec2(texelFetch(triangles, index * 9 + k).w,
                      texelFetch(triangles, index * 9 + 3 + k).w) *
                 weights[k];
    int mat = int(texelFetch(instances, instance * 13 + 12).y);
    vec4 flags = texelFetch(materials, mat * 5 + 4);
    mat4 world =
        mat4(texelFetch(instances, instance * 13 + 4), texelFetch(instances, instance * 13 + 5),
             texelFetch(instances, instance * 13 + 6), texelFetch(instances, instance * 13 + 7));

    if (flags.z < .5 && det * (determinant(mat3(world)) < 0. ? -1. : 1.) < 0.)
        return false;
    if (cutout(mat, coord))
        return false;
    hit.distance = t;
    hit.triangle = index;
    hit.instance = instance;
    hit.bary = vec2(u, v);
    return true;
}

void traceMesh(vec3 origin, vec3 direction, int instance, inout TraceHit hit) {
    mat4 inverseWorld =
        mat4(texelFetch(instances, instance * 13), texelFetch(instances, instance * 13 + 1),
             texelFetch(instances, instance * 13 + 2), texelFetch(instances, instance * 13 + 3));
    vec3 o = (inverseWorld * vec4(origin, 1)).xyz, d = (inverseWorld * vec4(direction, 0)).xyz;
    int stack[64];
    int size = 1;
    stack[0] = int(texelFetch(instances, instance * 13 + 12).x);

    while (size > 0) {
        int node = stack[--size];
        vec4 a = texelFetch(blasNodes, node * 3), b = texelFetch(blasNodes, node * 3 + 1),
             c = texelFetch(blasNodes, node * 3 + 2);

        if (!boxHit(o, d, a.xyz, b.xyz, hit.distance))
            continue;
        if (c.y > 0.) {
            for (int j = 0; j < int(c.y); ++j)
                triangleHit(o, d, int(c.x) + j, hit, instance);
        } else {
            stack[size++] = int(a.w);
            stack[size++] = int(b.w);
        }
    }
}

TraceHit trace(vec3 o, vec3 d) {
    TraceHit hit;
    hit.distance = 1e20;
    hit.triangle = -1;
    hit.instance = -1;
    hit.bary = vec2(0);

    if (instanceCount == 0)
        return hit;
    int stack[64];
    int size = 1;
    stack[0] = 0;

    while (size > 0) {
        int node = stack[--size];
        vec4 a = texelFetch(tlasNodes, node * 3), b = texelFetch(tlasNodes, node * 3 + 1),
             c = texelFetch(tlasNodes, node * 3 + 2);

        if (!boxHit(o, d, a.xyz, b.xyz, hit.distance))
            continue;
        if (c.y > 0.)
            for (int j = 0; j < int(c.y); ++j)
                traceMesh(o, d, int(texelFetch(instanceOrder, int(c.x) + j).x), hit);
        else {
            stack[size++] = int(a.w);
            stack[size++] = int(b.w);
        }
    }

    return hit;
}

vec3 incoming(vec3 o, vec3 d) {
    TraceHit hit = trace(o, d);

    if (hit.triangle < 0)
        return textureLod(environmentMap, sphereUV(d), 0.).rgb * environmentIntensity;
    vec3 weights = vec3(1. - hit.bary.x - hit.bary.y, hit.bary.x, hit.bary.y), n = vec3(0);
    vec4 tangent = vec4(0);
    vec2 coord = vec2(0);

    for (int k = 0; k < 3; ++k) {
        vec4 p = texelFetch(triangles, hit.triangle * 9 + k),
             normal = texelFetch(triangles, hit.triangle * 9 + 3 + k);
        coord += vec2(p.w, normal.w) * weights[k];
        n += normal.xyz * weights[k];
        tangent += texelFetch(triangles, hit.triangle * 9 + 6 + k) * weights[k];
    }

    int instance = hit.instance;
    mat4 inverseWorld =
        mat4(texelFetch(instances, instance * 13), texelFetch(instances, instance * 13 + 1),
             texelFetch(instances, instance * 13 + 2), texelFetch(instances, instance * 13 + 3));
    mat4 world =
        mat4(texelFetch(instances, instance * 13 + 4), texelFetch(instances, instance * 13 + 5),
             texelFetch(instances, instance * 13 + 6), texelFetch(instances, instance * 13 + 7));
    n = normalize(transpose(mat3(inverseWorld)) * n);

    if (dot(n, d) > 0.)
        n = -n;
    tangent = vec4(normalize(mat3(world) * tangent.xyz),
                   tangent.w * (determinant(mat3(world)) < 0. ? -1. : 1.));
    int mat = int(texelFetch(instances, instance * 13 + 12).y);
    Surface s = surfaceAt(mat, coord, n, tangent, 0.);
    return s.emissive + directLighting(s, o + d * hit.distance, -d);
}

void main() {
    reflection = vec4(0);
    gi = vec4(0);
    float depth = texture(depthMap, uv).r;

    if (depth >= 1.)
        return;

    vec4 n = texture(normalRoughness, uv), b = texture(baseMetallic, uv);
    Surface s;
    s.normal = normalize(n.xyz);
    s.roughness = n.w;
    s.base = b.rgb;
    s.metallic = b.w;
    s.ao = 1.;
    vec3 p = worldPosition(uv, depth), v = normalize(cameraPosition - p);
    vec3 o = p + s.normal * .001;
    rng = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u + uint(frameIndex) * 26699u;
    rng |= 1u;
    mat3 local = basis(s.normal);
    vec4 previous = previousViewProjection * vec4(p, 1);
    vec2 previousUV = previous.xy / previous.w * .5 + .5;
    float blend = frameIndex == 0 || any(lessThan(previousUV, vec2(0))) ||
                          any(greaterThan(previousUV, vec2(1)))
                      ? 1.
                      : .1;

    if (useGI) {
        float angle = 2. * PI * randomValue(), r = sqrt(randomValue());
        vec3 d = local * vec3(r * cos(angle), r * sin(angle), sqrt(max(0., 1. - r * r)));
        vec3 value = min(incoming(o, d), vec3(50));
        gi = vec4(mix(texture(giHistory, previousUV).rgb, value, blend), 1);
    }

    if (useReflections) {
        float u = randomValue(), angle = 2. * PI * randomValue(), a = s.roughness * s.roughness;
        float cosine = sqrt((1. - u) / (1. + (a * a - 1.) * u));
        vec3 h = normalize(local * vec3(sqrt(max(0., 1. - cosine * cosine)) * cos(angle),
                                        sqrt(max(0., 1. - cosine * cosine)) * sin(angle), cosine));
        vec3 d = reflect(-v, h);
        float nl = max(dot(s.normal, d), 0.), nv = max(dot(s.normal, v), 1e-4),
              nh = max(dot(s.normal, h), 1e-4), vh = max(dot(v, h), 1e-4);
        vec3 f = fresnel(vh, mix(vec3(.04), s.base, s.metallic));
        float weight =
            geometryTerm(nv, s.roughness) * geometryTerm(nl, s.roughness) * vh / max(nv * nh, 1e-5);
        vec3 value = nl > 0. ? min(incoming(o, d) * f * weight, vec3(50)) : vec3(0);
        reflection = vec4(mix(texture(reflectionHistory, previousUV).rgb, value, blend), 1);
    }
}
