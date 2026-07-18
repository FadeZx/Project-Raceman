#version 450 core
out vec4 FragColor;
in vec2 vUV;
in vec3 vWorldNormal;
uniform vec4 uColor;
uniform vec3 uAmbientColor;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
void main() {
    vec4 base = uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0);
    float facing = max(normalize(vWorldNormal).y * 0.5 + 0.5, 0.25);
    FragColor = vec4(base.rgb * uColor.rgb * (uAmbientColor + vec3(facing)), base.a * uColor.a);
}
