#version 450 core
out vec4 FragColor;
in vec2 vUV;
in vec3 vWorldPosition;
in vec3 vWorldNormal;

uniform vec4 uColor; // RGBA
uniform vec3 uEmissiveColor;
uniform float uMetallic;
uniform float uRoughness;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
uniform sampler2D uMaterialAlbedoTexture;
uniform bool uUseMaterialAlbedoTexture;
uniform float uAlphaCutoff;
uniform bool uUnlit;
uniform vec3 uCameraPosition;

struct Light {
    int type; // 0 directional, 1 point, 2 spot
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};

uniform vec3 uAmbientColor;
uniform int uLightCount;
uniform Light uLights[8];

void main() {
    vec4 base = uUseMaterialAlbedoTexture ? texture(uMaterialAlbedoTexture, vUV) : (uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0));
    vec4 albedo = base * uColor;
    if (uAlphaCutoff > 0.0 && albedo.a < uAlphaCutoff) {
        discard;
    }
    if (uUnlit) {
        FragColor = albedo;
        return;
    }

    vec3 normal = normalize(vWorldNormal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.02, 1.0);
    float shininess = mix(96.0, 8.0, roughness);
    vec3 specularColor = mix(vec3(0.04), albedo.rgb, metallic);
    vec3 lit = albedo.rgb * uAmbientColor;

    for (int i = 0; i < uLightCount; ++i) {
        Light light = uLights[i];
        vec3 lightDir;
        float attenuation = 1.0;

        if (light.type == 0) {
            lightDir = normalize(-light.direction);
        } else {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = length(toLight);
            if (distanceToLight > max(light.range, 0.001)) {
                continue;
            }
            lightDir = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
            float rangeFactor = clamp(1.0 - distanceToLight / max(light.range, 0.001), 0.0, 1.0);
            attenuation = rangeFactor * rangeFactor;

            if (light.type == 2) {
                float cosTheta = dot(normalize(light.direction), normalize(vWorldPosition - light.position));
                float cutoff = cos(radians(clamp(light.spotAngleDegrees, 1.0, 179.0)) * 0.5);
                float spot = smoothstep(cutoff, min(cutoff + 0.08, 1.0), cosTheta);
                attenuation *= spot;
            }
        }

        float ndotl = max(dot(normal, lightDir), 0.0);
        vec3 halfDir = normalize(lightDir + viewDir);
        float specular = pow(max(dot(normal, halfDir), 0.0), shininess) * ndotl * mix(0.2, 1.0, 1.0 - roughness);
        float broadSpecular = ndotl * metallic * mix(0.05, 0.45, roughness);
        vec3 radiance = light.color * max(light.intensity, 0.0) * attenuation;
        vec3 diffuse = albedo.rgb * ndotl * (1.0 - metallic);
        lit += (diffuse + specularColor * (specular + broadSpecular)) * radiance;
    }

    FragColor = vec4(lit + uEmissiveColor, albedo.a);
}
