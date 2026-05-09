#version 450 core
out vec4 FragColor;
in vec2 vUV;
uniform vec4 uColor;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
void main() {
    vec4 base = uUseDiffuseTexture ? texture(uDiffuseTexture, vUV) : vec4(1.0);
    FragColor = base * uColor;
}
