#version 450 core
out vec4 FragColor;
in vec2 vUV;
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
    FragColor = vec4(albedo.rgb + emissive, albedo.a);
}
