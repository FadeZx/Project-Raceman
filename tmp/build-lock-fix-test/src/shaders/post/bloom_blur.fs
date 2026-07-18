#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uImage;
uniform vec2 uTexelStep;

void main() {
    const float weights[5] = float[](0.22702703, 0.19459459, 0.12162162, 0.05405405, 0.01621622);
    vec3 result = texture(uImage, vUV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uTexelStep * float(i);
        result += texture(uImage, vUV + offset).rgb * weights[i];
        result += texture(uImage, vUV - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
