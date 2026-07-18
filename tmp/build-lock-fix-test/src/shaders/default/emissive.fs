#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
in vec2 vUV;
in vec3 vWorldNormal;
uniform vec4 uColor;
uniform vec3 uEmissiveColor;
uniform float uEmissiveIntensity;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
uniform sampler2D uEmissiveTexture;
uniform bool uUseEmissiveTexture;
void main() {
    vec4 base = uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0);
    vec4 albedo = base * uColor;
    vec3 emissiveMap = uUseEmissiveTexture ? texture(uEmissiveTexture, vUV).rgb : vec3(1.0);
    vec3 emissive = uEmissiveColor * emissiveMap * max(uEmissiveIntensity, 0.0);
    NormalBuffer = vec4(normalize(vWorldNormal), 1.0);
    AmbientBuffer = vec4(0.0, 0.0, 0.0, albedo.a);
    FragColor = vec4(albedo.rgb + emissive, albedo.a);
}
