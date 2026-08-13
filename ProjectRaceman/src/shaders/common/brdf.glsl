// Shared microfacet BRDF terms.
//
// Included, not compiled on its own: the Shader loader splices this in and
// strips any #version, so callers own the version directive. Include with
//   #include <common/brdf.glsl>
// which resolves against the engine shader root and therefore works from
// project-authored and graph-generated shaders as well.

#ifndef RACEMAN_COMMON_BRDF
#define RACEMAN_COMMON_BRDF

const float PI = 3.14159265359;

// Trowbridge-Reitz GGX normal distribution.
float DistributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.000001);
}

// Schlick-GGX geometry term with the direct-lighting k remapping.
float GeometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
    return GeometrySchlickGGX(max(dot(normal, viewDir), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(normal, lightDir), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Roughness-aware variant used for image-based lighting, where the reflected
// lobe is an average over directions rather than a single mirror direction.
vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
        pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

#endif
