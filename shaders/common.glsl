const float PI = 3.14159265359;
uniform samplerBuffer materials;
uniform sampler2DArray baseMaps, normalMaps, mrMaps, aoMaps, emissiveMaps;
uniform sampler2D environmentMap, irradianceMap, prefilterMap, brdfMap;
uniform sampler2DArrayShadow shadowMaps;
uniform vec3 cameraPosition;
uniform mat4 viewProjection, invViewProjection, previousViewProjection;
uniform vec3 lightPositions[32], lightDirections[32], lightColors[32];
uniform vec4 lightParameters[32]; // type range inner cosine outer cosine
uniform int lightShadowBase[32], lightShadowCount[32];
uniform mat4 shadowMatrices[16];
uniform int lightCount;
uniform float environmentIntensity;

struct Surface {
    vec3 base;
    vec3 normal;
    vec3 emissive;
    float metallic;
    float roughness;
    float ao;
    float alpha;
};

vec2 sphereUV(vec3 d) {
    return vec2(atan(d.z, d.x) / (2.0 * PI) + .5, acos(clamp(d.y, -1., 1.)) / PI);
}

vec3 environment(vec3 d, float lod) {
    return textureLod(prefilterMap, sphereUV(d), lod).rgb * environmentIntensity;
}

vec3 irradiance(vec3 n) {
    return textureLod(irradianceMap, sphereUV(n), 0.).rgb * environmentIntensity;
}

Surface surfaceAt(int mat, vec2 uv, vec3 n, vec4 tangent, float lod) {
    vec4 a = texelFetch(materials, mat * 5), b = texelFetch(materials, mat * 5 + 1),
         c = texelFetch(materials, mat * 5 + 2), layers = texelFetch(materials, mat * 5 + 3),
         extra = texelFetch(materials, mat * 5 + 4);
    vec4 base = textureLod(baseMaps, vec3(uv, layers.x), lod) * a;
    vec4 mr = textureLod(mrMaps, vec3(uv, layers.z), lod);
    vec3 normal = textureLod(normalMaps, vec3(uv, layers.y), lod).xyz * 2. - 1.;
    normal.xy *= c.y;
    vec3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
    Surface s;
    s.normal = normalize(mat3(t, cross(n, t) * tangent.w, n) * normal);
    s.base = base.rgb;
    s.alpha = base.a;
    s.metallic = clamp(b.w * mr.b, 0., 1.);
    s.roughness = clamp(c.x * mr.g, .045, 1.);
    s.ao = mix(1., textureLod(aoMaps, vec3(uv, layers.w), lod).r, c.z);
    s.emissive = b.xyz * textureLod(emissiveMaps, vec3(uv, extra.x), lod).rgb;
    return s;
}

bool cutout(int mat, vec2 uv) {
    vec4 extra = texelFetch(materials, mat * 5 + 4);

    if (int(extra.y) != 1)
        return false;
    vec4 a = texelFetch(materials, mat * 5), layers = texelFetch(materials, mat * 5 + 3),
         c = texelFetch(materials, mat * 5 + 2);
    return textureLod(baseMaps, vec3(uv, layers.x), 0.).a * a.a < c.w;
}

vec3 fresnel(float cosine, vec3 f0) {
    return f0 + (1. - f0) * pow(clamp(1. - cosine, 0., 1.), 5.);
}

float ggx(float nh, float rough) {
    float a = rough * rough, a2 = a * a, d = nh * nh * (a2 - 1.) + 1.;
    return a2 / max(PI * d * d, 1e-7);
}

float geometryTerm(float nv, float rough) {
    float k = (rough + 1.) * (rough + 1.) / 8.;
    return nv / (nv * (1. - k) + k);
}

vec3 brdf(Surface s, vec3 v, vec3 l) {
    vec3 h = normalize(v + l);
    float nv = max(dot(s.normal, v), 1e-4), nl = max(dot(s.normal, l), 0.),
          nh = max(dot(s.normal, h), 0.);
    vec3 f = fresnel(max(dot(h, v), 0.), mix(vec3(.04), s.base, s.metallic));
    vec3 spec = ggx(nh, s.roughness) * geometryTerm(nv, s.roughness) *
                geometryTerm(nl, s.roughness) * f / max(4. * nv * nl, 1e-5);
    return ((1. - f) * (1. - s.metallic) * s.base / PI + spec) * nl;
}

int cubeFace(vec3 d) {
    vec3 a = abs(d);

    if (a.x >= a.y && a.x >= a.z)
        return d.x > 0. ? 0 : 1;
    if (a.y >= a.z)
        return d.y > 0. ? 2 : 3;
    return d.z > 0. ? 4 : 5;
}

float shadowVisibility(int light, vec3 p, vec3 normal) {
    int base = lightShadowBase[light];

    if (base < 0)
        return 1.;
    int index = base + (lightShadowCount[light] == 6 ? cubeFace(p - lightPositions[light]) : 0);
    vec4 clip = shadowMatrices[index] * vec4(p, 1);
    vec3 coord = clip.xyz / clip.w * .5 + .5;

    if (clip.w <= 0. || any(lessThan(coord, vec3(0))) || any(greaterThan(coord, vec3(1))))
        return 1.;
    vec3 l = int(lightParameters[light].x) == 0 ? -lightDirections[light]
                                                : normalize(lightPositions[light] - p);
    float bias = max(.0004 * (1. - dot(normal, l)), .00008), sum = 0.;
    vec2 texel = 1. / vec2(textureSize(shadowMaps, 0).xy);

    // overlapping bilinear comparisons smooth edges with four lookups
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            vec2 offset = vec2(float(x) - .5, float(y) - .5) * texel;
            sum += texture(shadowMaps, vec4(coord.xy + offset, index, coord.z - bias));
        }

    return sum * .25;
}

vec3 directLighting(Surface s, vec3 p, vec3 v) {
    vec3 result = vec3(0);

    for (int i = 0; i < lightCount; ++i) {
        int type = int(lightParameters[i].x);
        vec3 delta = lightPositions[i] - p;
        float distance = length(delta);
        vec3 l = type == 0 ? -lightDirections[i] : delta / max(distance, 1e-5);
        float attenuation = 1.;

        if (type != 0) {
            float cutoff = clamp(1. - pow(distance / lightParameters[i].y, 4.), 0., 1.);
            attenuation = cutoff * cutoff / max(distance * distance, .01);
        }

        if (type == 2)
            attenuation *=
                smoothstep(lightParameters[i].w, lightParameters[i].z, dot(-l, lightDirections[i]));
        result += brdf(s, v, l) * lightColors[i] * attenuation * shadowVisibility(i, p, s.normal);
    }

    return result;
}

vec3 diffuseWeight(Surface s, vec3 v) {
    return (1. - fresnel(max(dot(s.normal, v), 0.), mix(vec3(.04), s.base, s.metallic))) *
           (1. - s.metallic);
}

vec3 specularIBL(Surface s, vec3 v) {
    float nv = max(dot(s.normal, v), 0.);
    vec3 f0 = mix(vec3(.04), s.base, s.metallic);
    vec2 lut = texture(brdfMap, vec2(nv, s.roughness)).rg;
    return environment(reflect(-v, s.normal), s.roughness * 8.) * (f0 * lut.x + lut.y) * s.ao;
}

vec3 worldPosition(vec2 uv, float depth) {
    vec4 p = invViewProjection * vec4(uv * 2. - 1., depth * 2. - 1., 1);
    return p.xyz / p.w;
}
