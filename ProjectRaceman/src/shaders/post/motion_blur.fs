#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uVelocityTexture;
uniform vec2 uResolution;
uniform float uVelocityScale;
uniform float uMaxRadiusPixels;
uniform int uSampleCount;
uniform bool uDebugView;

void main() {
    vec2 velocity = texture(uVelocityTexture, vUV).xy * uVelocityScale;
    float radiusPixels = length(velocity * uResolution);
    if (radiusPixels > uMaxRadiusPixels && radiusPixels > 0.0001) {
        velocity *= uMaxRadiusPixels / radiusPixels;
        radiusPixels = uMaxRadiusPixels;
    }
    if (uDebugView) {
        vec2 direction = radiusPixels > 0.0001 ? normalize(velocity) : vec2(0.0);
        FragColor = vec4(direction * 0.5 + 0.5, clamp(radiusPixels / max(uMaxRadiusPixels, 1.0), 0.0, 1.0), 1.0);
        return;
    }
    if (radiusPixels < 0.5) {
        FragColor = texture(uSceneTexture, vUV);
        return;
    }
    vec4 color = vec4(0.0);
    int samples = clamp(uSampleCount, 4, 32);
    for (int sampleIndex = 0; sampleIndex < 32; ++sampleIndex) {
        if (sampleIndex >= samples) break;
        float t = (float(sampleIndex) / float(samples - 1)) - 0.5;
        color += texture(uSceneTexture, clamp(vUV - velocity * t, vec2(0.0), vec2(1.0)));
    }
    FragColor = color / float(samples);
}
