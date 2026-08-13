#version 450 core

// Unlit still participates in fog: fog is atmospheric occlusion between the
// camera and the surface, not lighting applied to it. A surface that opts out of
// scene lights should still be hidden by the air in front of it, otherwise a
// blockout built from unlit materials has no depth cue at all. The Unlit Scene
// View mode is unaffected — ResolveProfileForTarget turns fog off for it.
#include <common/fog.glsl>

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
layout(location = 3) out vec4 MaterialBuffer;
in vec2 vUV;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
uniform vec3 uCameraPosition;
uniform vec4 uColor;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
uniform sampler2D uMaterialAlbedoTexture;
uniform bool uUseMaterialAlbedoTexture;
uniform float uAlphaCutoff;
void main() {
    vec4 base = uUseMaterialAlbedoTexture ? texture(uMaterialAlbedoTexture, vUV) : (uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0));
    vec4 albedo = base * uColor;
    if (uAlphaCutoff > 0.0 && albedo.a < uAlphaCutoff) {
        discard;
    }
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float fogTransmittance = FogTransmittance(uCameraPosition, vWorldPosition);
    NormalBuffer = vec4(normalize(vWorldNormal), 1.0);
    AmbientBuffer = vec4(0.0, 0.0, 0.0, albedo.a);
    MaterialBuffer = vec4(0.0, 1.0, 0.0, 0.0);
    FragColor = vec4(albedo.rgb * fogTransmittance +
        FogInscatter(viewDirection) * (1.0 - fogTransmittance), albedo.a);
    if (uFogDebugView) FragColor = vec4(vec3(fogTransmittance), albedo.a);
}
