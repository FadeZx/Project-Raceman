#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
in vec2 vUV;
in vec3 vWorldNormal;
uniform vec4 uColor;
uniform vec3 uAmbientColor;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
void main() {
    vec4 base = uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0);
    float facing = max(normalize(vWorldNormal).y * 0.5 + 0.5, 0.25);
    NormalBuffer = vec4(normalize(vWorldNormal), 1.0);
    AmbientBuffer = vec4(0.0, 0.0, 0.0, base.a * uColor.a);
    FragColor = vec4(base.rgb * uColor.rgb * (uAmbientColor + vec3(facing)), base.a * uColor.a);
}
