#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uDepthTexture;
uniform mat4 uInverseProjection;
uniform vec2 uTexelSize;
uniform float uFocusDistance;
uniform float uFocusRange;
uniform float uMaxRadiusPixels;
uniform int uSampleCount;

float ViewDepth(vec2 uv) {
    float depth = texture(uDepthTexture, uv).r;
    vec4 view = uInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return abs(view.z / max(abs(view.w), 0.00001));
}

void main() {
    float viewDepth = ViewDepth(vUV);
    float coc = clamp(abs(viewDepth - uFocusDistance) / max(uFocusRange, 0.01), 0.0, 1.0);
    float radius = coc * uMaxRadiusPixels;
    vec4 center = texture(uSceneTexture, vUV);
    if (radius < 0.5) {
        FragColor = center;
        return;
    }
    const vec2 disk[16] = vec2[](
        vec2(0.0, 0.0), vec2(0.5278, -0.0859), vec2(-0.0401, 0.5361), vec2(-0.6704, -0.1799),
        vec2(0.1696, -0.7468), vec2(0.8074, 0.3118), vec2(-0.5686, 0.6966), vec2(-0.9087, -0.4920),
        vec2(0.4118, 0.9123), vec2(0.9679, -0.5032), vec2(-0.2213, -0.9631), vec2(-0.9894, 0.1254),
        vec2(0.7041, 0.7101), vec2(-0.7112, -0.7030), vec2(0.2727, -0.3784), vec2(-0.3571, 0.2679));
    vec4 color = vec4(0.0);
    int samples = clamp(uSampleCount, 4, 16);
    for (int i = 0; i < 16; ++i) {
        if (i >= samples) break;
        color += texture(uSceneTexture, clamp(vUV + disk[i] * uTexelSize * radius, vec2(0.0), vec2(1.0)));
    }
    FragColor = color / float(samples);
}
