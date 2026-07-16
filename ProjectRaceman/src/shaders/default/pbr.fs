#version 450 core

layout(location = 0) out vec4 FragColor;
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
        directLighting += (diffuseWeight * albedo / PI + specular) * radiance * nDotL;
    }

    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 ambientFresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 ambientDiffuseWeight = (vec3(1.0) - ambientFresnel) * (1.0 - metallic);
    vec3 ambientDiffuse = ambientDiffuseWeight * albedo * uAmbientColor;
    // Temporary diffuse-environment approximation. This keeps car paint and
    // metals readable until irradiance and prefiltered reflection maps are
    // connected to the PBR shader.
    vec3 ambientSpecular = ambientFresnel * uAmbientColor *
        mix(0.35, 1.0, 1.0 - roughness);
    vec3 ambient = (ambientDiffuse + ambientSpecular) * ao;
    if (uStylized) {
        float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
        ambient += albedo * rim * max(uStylizedRimStrength, 0.0);
    }
    FragColor = vec4(ambient + directLighting + uEmissiveColor, albedoSample.a);
}
