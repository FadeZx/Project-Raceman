#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
layout(location = 3) out vec4 MaterialBuffer;
in vec2 vUV;
in vec3 vWorldNormal;
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
    NormalBuffer = vec4(normalize(vWorldNormal), 1.0);
    AmbientBuffer = vec4(0.0, 0.0, 0.0, albedo.a);
    MaterialBuffer = vec4(0.0, 1.0, 0.0, 0.0);
    FragColor = albedo;
}
