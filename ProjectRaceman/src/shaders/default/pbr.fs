#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
layout(location = 3) out vec4 MaterialBuffer;
in vec2 vUV;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vWorldTangent;
in vec3 vWorldBitangent;

uniform vec4 uColor;
uniform vec3 uEmissiveColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uClearCoat;
uniform float uClearCoatRoughness;
uniform float uAnisotropy;
uniform float uTransmission;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
uniform sampler2D uMaterialAlbedoTexture;
uniform bool uUseMaterialAlbedoTexture;
uniform sampler2D uMaterialNormalTexture;
uniform sampler2D uMaterialMetallicTexture;
uniform sampler2D uMaterialRoughnessTexture;
uniform sampler2D uMaterialAoTexture;
uniform bool uUseMaterialNormalTexture;
uniform bool uUseMaterialMetallicTexture;
uniform bool uUseMaterialRoughnessTexture;
uniform bool uUseMaterialAoTexture;
uniform float uAlphaCutoff;
uniform vec3 uCameraPosition;
uniform vec3 uAmbientColor;
uniform bool uStylized;
uniform float uStylizedBands;
uniform float uStylizedRimStrength;
uniform bool uEnableDirectionalShadow;
uniform int uShadowLightIndex;
uniform mat4 uView;
uniform mat4 uDirectionalLightMatrices[4];
uniform float uShadowCascadeSplits[4];
uniform int uShadowCascadeCount;
uniform sampler2DArray uDirectionalShadowMap;
uniform float uShadowSoftness;
uniform bool uShadowCascadeDebugView;
uniform int uSpotShadowCount;
uniform int uSpotShadowLightIndices[4];
uniform mat4 uSpotLightMatrices[4];
uniform sampler2DArray uSpotShadowMap;
uniform int uPointShadowCount;
uniform int uPointShadowLightIndices[4];
uniform vec3 uPointShadowPositions[4];
uniform float uPointShadowRanges[4];
uniform samplerCubeArray uPointShadowMap;
uniform bool uEnableIbl;
uniform bool uUseBakedIbl;
uniform samplerCube uEnvironmentMap;
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilterMap;
uniform sampler2D uBrdfLut;
uniform int uReflectionProbeCount;
uniform samplerCube uReflectionProbeMaps[4];
uniform vec3 uReflectionProbePositions[4];
// 0 = box, 1 = sphere (extents.x carries the sphere radius).
uniform int uReflectionProbeShapes[4];
uniform vec3 uReflectionProbeExtents[4];
uniform float uReflectionProbeBlendDistances[4];
uniform float uReflectionProbeIntensities[4];
uniform float uEnvironmentIntensity;
uniform float uReflectionIntensity;
uniform int uIblDebugMode;

struct Light {
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};

uniform int uLightCount;
uniform Light uLights[8];

const float PI = 3.14159265359;

vec3 BoxProjectedReflectionDirection(vec3 worldPosition, vec3 reflectionDirection,
                                     vec3 probePosition, vec3 boxExtents) {
    vec3 safeDirection = vec3(
        abs(reflectionDirection.x) > 0.0001 ? reflectionDirection.x : (reflectionDirection.x >= 0.0 ? 0.0001 : -0.0001),
        abs(reflectionDirection.y) > 0.0001 ? reflectionDirection.y : (reflectionDirection.y >= 0.0 ? 0.0001 : -0.0001),
        abs(reflectionDirection.z) > 0.0001 ? reflectionDirection.z : (reflectionDirection.z >= 0.0 ? 0.0001 : -0.0001));
    vec3 boxMinimum = probePosition - boxExtents;
    vec3 boxMaximum = probePosition + boxExtents;
    vec3 targetPlane = vec3(
        safeDirection.x > 0.0 ? boxMaximum.x : boxMinimum.x,
        safeDirection.y > 0.0 ? boxMaximum.y : boxMinimum.y,
        safeDirection.z > 0.0 ? boxMaximum.z : boxMinimum.z);
    vec3 distances = (targetPlane - worldPosition) / safeDirection;
    float intersectionDistance = min(distances.x, min(distances.y, distances.z));
    vec3 intersection = worldPosition + safeDirection * max(intersectionDistance, 0.0);
    return intersection - probePosition;
}

vec3 SphereProjectedReflectionDirection(vec3 worldPosition, vec3 reflectionDirection,
                                        vec3 probePosition, float radius) {
    // Ray-sphere intersection from inside the influence sphere; the hit point
    // re-centres the lookup so the reflection parallax matches the capture point.
    vec3 fromCenter = worldPosition - probePosition;
    float b = dot(fromCenter, reflectionDirection);
    float c = dot(fromCenter, fromCenter) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant <= 0.0) return reflectionDirection;
    float hitDistance = -b + sqrt(discriminant);
    return fromCenter + reflectionDirection * max(hitDistance, 0.0);
}

vec4 SampleLocalReflectionProbes(vec3 worldPosition, vec3 reflectionDirection, float roughness) {
    vec3 accumulated = vec3(0.0);
    float accumulatedWeight = 0.0;
    float accumulatedCoverage = 0.0;
    float maximumMip = uUseBakedIbl ? 4.0 : 7.0;
    for (int probeIndex = 0; probeIndex < 4; ++probeIndex) {
        if (probeIndex >= uReflectionProbeCount) break;
        bool isSphere = uReflectionProbeShapes[probeIndex] == 1;
        vec3 localPosition = worldPosition - uReflectionProbePositions[probeIndex];
        float nearestBoundary;
        if (isSphere) {
            nearestBoundary = uReflectionProbeExtents[probeIndex].x - length(localPosition);
        } else {
            vec3 distanceInside = uReflectionProbeExtents[probeIndex] - abs(localPosition);
            nearestBoundary = min(distanceInside.x, min(distanceInside.y, distanceInside.z));
        }
        if (nearestBoundary <= 0.0) continue;
        float weight = smoothstep(0.0, max(uReflectionProbeBlendDistances[probeIndex], 0.01), nearestBoundary);
        float probeIntensity = max(uReflectionProbeIntensities[probeIndex], 0.0);
        vec3 correctedDirection = isSphere
            ? SphereProjectedReflectionDirection(worldPosition, reflectionDirection,
                uReflectionProbePositions[probeIndex], uReflectionProbeExtents[probeIndex].x)
            : BoxProjectedReflectionDirection(worldPosition, reflectionDirection,
                uReflectionProbePositions[probeIndex], uReflectionProbeExtents[probeIndex]);
        accumulated += textureLod(uReflectionProbeMaps[probeIndex], correctedDirection, roughness * maximumMip).rgb *
            weight * probeIntensity;
        accumulatedWeight += weight;
        accumulatedCoverage += weight * clamp(probeIntensity, 0.0, 1.0);
    }
    return vec4(accumulated / max(accumulatedWeight, 0.0001), clamp(accumulatedCoverage, 0.0, 1.0));
}

float DistributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.000001);
}

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

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
        pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

mat3 BuildTbn(vec3 normal) {
    vec3 tangent = vWorldTangent;
    vec3 bitangent = vWorldBitangent;
    if (dot(tangent, tangent) < 0.01 || dot(bitangent, bitangent) < 0.01) {
        vec3 dp1 = dFdx(vWorldPosition);
        vec3 dp2 = dFdy(vWorldPosition);
        vec2 duv1 = dFdx(vUV);
        vec2 duv2 = dFdy(vUV);
        tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
        bitangent = normalize(-dp1 * duv2.x + dp2 * duv1.x);
    } else {
        tangent = normalize(tangent - normal * dot(normal, tangent));
        bitangent = normalize(bitangent - normal * dot(normal, bitangent));
        if (dot(cross(normal, tangent), bitangent) < 0.0) bitangent = -bitangent;
    }
    return mat3(tangent, bitangent, normal);
}

int ShadowCascadeIndex(vec3 worldPosition) {
    float viewDepth = abs((uView * vec4(worldPosition, 1.0)).z);
    for (int cascade = 0; cascade < min(uShadowCascadeCount, 4); ++cascade) {
        if (viewDepth <= uShadowCascadeSplits[cascade]) return cascade;
    }
    return -1;
}

float DirectionalShadow(vec3 worldPosition, vec3 normal, vec3 lightDir) {
    int cascade = ShadowCascadeIndex(worldPosition);
    if (cascade < 0) return 0.0;
    vec4 lightClipPosition = uDirectionalLightMatrices[cascade] * vec4(worldPosition, 1.0);
    vec3 projected = lightClipPosition.xyz / lightClipPosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        projected.x <= 0.0 || projected.x >= 1.0 ||
        projected.y <= 0.0 || projected.y >= 1.0) return 0.0;

    float bias = max(0.0015 * (1.0 - dot(normal, lightDir)), 0.00025);
    vec2 texelSize = 1.0 / vec2(textureSize(uDirectionalShadowMap, 0).xy);
    if (uShadowSoftness <= 0.05) {
        float closestDepth = texture(uDirectionalShadowMap, vec3(projected.xy, float(cascade))).r;
        return projected.z - bias > closestDepth ? 1.0 : 0.0;
    }

    const vec2 poissonDisk[16] = vec2[](
        vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
        vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
        vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
        vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
    );
    float angle = fract(sin(dot(worldPosition.xz, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    mat2 rotation = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rotation * poissonDisk[i] * texelSize * uShadowSoftness;
        float closestDepth = texture(uDirectionalShadowMap,
            vec3(projected.xy + offset, float(cascade))).r;
        shadow += projected.z - bias > closestDepth ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

float SpotShadow(int slot, vec3 worldPosition, vec3 normal, vec3 lightDir) {
    vec4 clip = uSpotLightMatrices[slot] * vec4(worldPosition, 1.0);
    vec3 projected = clip.xyz / max(abs(clip.w), 0.00001);
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 || any(lessThanEqual(projected.xy, vec2(0.0))) || any(greaterThanEqual(projected.xy, vec2(1.0)))) return 0.0;
    float bias = max(0.002 * (1.0 - dot(normal, lightDir)), 0.0004);
    vec2 texel = 1.0 / vec2(textureSize(uSpotShadowMap, 0).xy);
    float radius = max(uShadowSoftness * 0.6, 0.0);
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(uSpotShadowMap, vec3(projected.xy + vec2(x, y) * texel * radius, float(slot))).r;
            shadow += projected.z - bias > closest ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

float PointShadow(int slot, vec3 worldPosition, vec3 normal, vec3 lightDir) {
    vec3 fromLight = worldPosition - uPointShadowPositions[slot];
    float range = max(uPointShadowRanges[slot], 0.001);
    float current = length(fromLight) / range;
    if (current >= 1.0) return 0.0;
    float bias = max(0.006 * (1.0 - dot(normal, lightDir)), 0.0015);
    float diskRadius = (0.004 + current * 0.008) * max(uShadowSoftness, 0.25);
    const vec3 offsets[8] = vec3[](
        vec3(1,1,1), vec3(-1,1,1), vec3(1,-1,1), vec3(-1,-1,1),
        vec3(1,1,-1), vec3(-1,1,-1), vec3(1,-1,-1), vec3(-1,-1,-1));
    float shadow = 0.0;
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float closest = texture(uPointShadowMap, vec4(fromLight + offsets[sampleIndex] * diskRadius * range, float(slot))).r;
        shadow += current - bias > closest ? 1.0 : 0.0;
    }
    return shadow / 8.0;
}

void main() {
    vec4 base = uUseMaterialAlbedoTexture
        ? texture(uMaterialAlbedoTexture, vUV)
        : (uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0));
    vec4 albedoSample = base * uColor;
    if (uAlphaCutoff > 0.0 && albedoSample.a < uAlphaCutoff) discard;

    vec3 normal = normalize(vWorldNormal);
    if (!gl_FrontFacing) normal = -normal;
    if (uUseMaterialNormalTexture) {
        vec3 tangentNormal = texture(uMaterialNormalTexture, vUV).xyz * 2.0 - 1.0;
        normal = normalize(BuildTbn(normal) * tangentNormal);
    }

    vec3 albedo = max(albedoSample.rgb, vec3(0.0));
    float metallic = clamp(uUseMaterialMetallicTexture ? texture(uMaterialMetallicTexture, vUV).r : uMetallic, 0.0, 1.0);
    float roughness = clamp(uUseMaterialRoughnessTexture ? texture(uMaterialRoughnessTexture, vUV).r : uRoughness, 0.045, 1.0);
    float ao = clamp(uUseMaterialAoTexture ? texture(uMaterialAoTexture, vUV).r : 1.0, 0.0, 1.0);
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 directLighting = vec3(0.0);

    for (int i = 0; i < uLightCount; ++i) {
        Light light = uLights[i];
        vec3 lightDir;
        float attenuation = 1.0;
        if (light.type == 0) {
            lightDir = normalize(-light.direction);
        } else {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = length(toLight);
            if (distanceToLight > max(light.range, 0.001)) continue;
            lightDir = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
            float normalizedDistance = distanceToLight / max(light.range, 0.001);
            attenuation = pow(clamp(1.0 - pow(normalizedDistance, 4.0), 0.0, 1.0), 2.0) /
                          max(distanceToLight * distanceToLight, 0.01);
            if (light.type == 2) {
                float cosTheta = dot(normalize(light.direction), normalize(vWorldPosition - light.position));
                float cutoff = cos(radians(clamp(light.spotAngleDegrees, 1.0, 179.0)) * 0.5);
                attenuation *= smoothstep(cutoff, min(cutoff + 0.08, 1.0), cosTheta);
            }
        }

        vec3 halfway = normalize(viewDir + lightDir);
        float nDotL = max(dot(normal, lightDir), 0.0);
        float nDotV = max(dot(normal, viewDir), 0.0);
        float distribution = DistributionGGX(normal, halfway, roughness);
        float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);
        vec3 fresnel = FresnelSchlick(max(dot(halfway, viewDir), 0.0), f0);
        vec3 specular = distribution * geometry * fresnel /
            max(4.0 * nDotV * nDotL, 0.0001);
        if (uClearCoat > 0.0) {
            float coatDistribution = DistributionGGX(normal, halfway, clamp(uClearCoatRoughness, 0.02, 1.0));
            float coatGeometry = GeometrySmith(normal, viewDir, lightDir, clamp(uClearCoatRoughness, 0.02, 1.0));
            vec3 coatFresnel = FresnelSchlick(max(dot(halfway, viewDir), 0.0), vec3(0.04));
            specular += coatDistribution * coatGeometry * coatFresnel /
                max(4.0 * nDotV * nDotL, 0.0001) * clamp(uClearCoat, 0.0, 1.0);
        }
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        diffuseWeight *= 1.0 - clamp(uTransmission, 0.0, 1.0);
        // Existing Raceman scenes authored directional intensities around 1.0
        // before the diffuse BRDF included its physically-correct 1 / PI term.
        // Convert those legacy values to radiance so old scenes retain their
        // authored brightness while using the GGX model.
        float legacyLightCalibration = light.type == 0 ? PI : 1.0;
        vec3 radiance = light.color * max(light.intensity, 0.0) * attenuation * legacyLightCalibration;

        if (uStylized) {
            nDotL = floor(nDotL * max(uStylizedBands, 2.0)) / max(uStylizedBands - 1.0, 1.0);
            specular = step(0.45 + roughness * 0.35, max(max(specular.r, specular.g), specular.b)) * fresnel;
        }
        float visibility = 1.0;
        if (uEnableDirectionalShadow && i == uShadowLightIndex && light.type == 0) {
            visibility = 1.0 - DirectionalShadow(vWorldPosition, normal, lightDir);
        }
        for (int slot = 0; slot < min(uSpotShadowCount, 4); ++slot) {
            if (i == uSpotShadowLightIndices[slot] && light.type == 2)
                visibility = 1.0 - SpotShadow(slot, vWorldPosition, normal, lightDir);
        }
        for (int slot = 0; slot < min(uPointShadowCount, 4); ++slot) {
            if (i == uPointShadowLightIndices[slot] && light.type == 1)
                visibility = 1.0 - PointShadow(slot, vWorldPosition, normal, lightDir);
        }
        directLighting += (diffuseWeight * albedo / PI + specular) * radiance * nDotL * visibility;
    }

    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 ambientFresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 ambientDiffuseWeight = (vec3(1.0) - ambientFresnel) * (1.0 - metallic);
    vec3 ambientDiffuse;
    vec3 ambientSpecular;
    // Keep the project ambient contribution independent from environment
    // reflections. Enabling IBL must add reflected light, not replace a
    // material's existing ambient visibility with a potentially darker sample.
    vec3 ambientFallbackSpecular = ambientFresnel * uAmbientColor *
        mix(0.35, 1.0, 1.0 - roughness);
    vec3 iblIrradiance = vec3(0.0);
    vec3 iblReflection = vec3(0.0);
    vec3 iblClearCoatReflection = vec3(0.0);
    vec3 rawPrefilteredReflection = vec3(0.0);
    if (uEnableIbl) {
        iblIrradiance = uUseBakedIbl
            ? texture(uIrradianceMap, normal).rgb
            : textureLod(uEnvironmentMap, normal, 5.0).rgb;
        vec3 reflectionDirection = reflect(-viewDir, normal);
        rawPrefilteredReflection = uUseBakedIbl
            ? textureLod(uPrefilterMap, reflectionDirection, roughness * 4.0).rgb
            : textureLod(uEnvironmentMap, reflectionDirection, roughness * 7.0).rgb;
        vec4 localProbeReflection = SampleLocalReflectionProbes(vWorldPosition, reflectionDirection, roughness);
        rawPrefilteredReflection = mix(rawPrefilteredReflection, localProbeReflection.rgb, localProbeReflection.a);
        vec2 environmentBrdf = texture(uBrdfLut, vec2(nDotV, roughness)).rg;
        iblReflection = rawPrefilteredReflection * (ambientFresnel * environmentBrdf.x + environmentBrdf.y);

        float coatRoughness = clamp(uClearCoatRoughness, 0.02, 1.0);
        vec3 coatFresnel = FresnelSchlickRoughness(nDotV, vec3(0.04), coatRoughness);
        vec3 coatPrefiltered = uUseBakedIbl
            ? textureLod(uPrefilterMap, reflectionDirection, coatRoughness * 4.0).rgb
            : textureLod(uEnvironmentMap, reflectionDirection, coatRoughness * 7.0).rgb;
        vec2 coatBrdf = texture(uBrdfLut, vec2(nDotV, coatRoughness)).rg;
        iblClearCoatReflection = coatPrefiltered * (coatFresnel * coatBrdf.x + coatBrdf.y) *
            clamp(uClearCoat, 0.0, 1.0);

        ambientDiffuse = ambientDiffuseWeight * albedo *
            (iblIrradiance * max(uEnvironmentIntensity, 0.0) + uAmbientColor);
        ambientSpecular = ambientFallbackSpecular +
            (iblReflection + iblClearCoatReflection) * max(uReflectionIntensity, 0.0);
    } else {
        ambientDiffuse = ambientDiffuseWeight * albedo * uAmbientColor;
        ambientSpecular = ambientFallbackSpecular;
    }
    vec3 ambient = (ambientDiffuse + ambientSpecular) * ao;
    if (uStylized) {
        float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
        ambient += albedo * rim * max(uStylizedRimStrength, 0.0);
    }
    if (uEnableIbl && uIblDebugMode == 1) {
        ambient = iblIrradiance;
        directLighting = vec3(0.0);
    } else if (uEnableIbl && uIblDebugMode == 2) {
        ambient = rawPrefilteredReflection;
        directLighting = vec3(0.0);
    } else if (uEnableIbl && uIblDebugMode == 3) {
        ambient = iblReflection + iblClearCoatReflection;
        directLighting = vec3(0.0);
    }
    NormalBuffer = vec4(normalize(normal), 1.0);
    AmbientBuffer = vec4(ambient, albedoSample.a);
    MaterialBuffer = vec4(metallic, roughness, clamp(uClearCoat, 0.0, 1.0), 1.0);
    FragColor = vec4(ambient + directLighting + uEmissiveColor, albedoSample.a);
    if (uEnableDirectionalShadow && uShadowCascadeDebugView) {
        const vec3 cascadeColors[4] = vec3[](
            vec3(1.0, 0.25, 0.25), vec3(0.25, 1.0, 0.35),
            vec3(0.25, 0.55, 1.0), vec3(1.0, 0.75, 0.2));
        int cascade = ShadowCascadeIndex(vWorldPosition);
        if (cascade >= 0) FragColor.rgb = mix(FragColor.rgb, FragColor.rgb * cascadeColors[cascade], 0.45);
    }
}
