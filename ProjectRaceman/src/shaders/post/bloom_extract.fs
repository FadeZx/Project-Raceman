#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uHdrScene;
uniform float uThreshold;

void main() {
    vec3 color = texture(uHdrScene, vUV).rgb;
    float brightness = max(max(color.r, color.g), color.b);
    float contribution = max(brightness - uThreshold, 0.0) / max(brightness, 0.0001);
    FragColor = vec4(color * contribution, 1.0);
}
