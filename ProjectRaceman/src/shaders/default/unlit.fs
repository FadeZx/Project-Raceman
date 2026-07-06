#version 450 core
out vec4 FragColor;
in vec2 vUV;
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
    FragColor = albedo;
}
