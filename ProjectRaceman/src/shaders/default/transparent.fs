#version 450 core

#include <common/fog.glsl>

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
layout(location = 3) out vec4 MaterialBuffer;
in vec2 vUV;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
uniform vec4 uColor;
uniform vec3 uEmissiveColor;
uniform float uRoughness;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
uniform vec3 uAmbientColor;
uniform vec3 uCameraPosition;
void main() {
    vec4 base = uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0);
    vec4 albedo = base * uColor;
    vec3 normalTint = normalize(vWorldNormal) * 0.5 + 0.5;
    vec3 color = albedo.rgb * (uAmbientColor + vec3(0.65)) + normalTint * (0.08 * (1.0 - clamp(uRoughness, 0.0, 1.0))) + uEmissiveColor;
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float fogTransmittance = FogTransmittance(uCameraPosition, vWorldPosition);
    NormalBuffer = vec4(normalize(vWorldNormal), 1.0);
    AmbientBuffer = vec4(0.0, 0.0, 0.0, albedo.a);
    MaterialBuffer = vec4(0.0, 1.0, 0.0, 0.0);
    color = color * fogTransmittance + FogInscatter(viewDirection) * (1.0 - fogTransmittance);
    FragColor = vec4(color, albedo.a);
    if (uFogDebugView) FragColor = vec4(vec3(fogTransmittance), albedo.a);
}
