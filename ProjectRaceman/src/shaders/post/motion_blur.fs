#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uVelocityTexture;
uniform sampler2D uDepthTexture;
uniform vec2 uResolution;
uniform float uVelocityScale;
uniform float uMaxRadiusPixels;
uniform float uMinimumVelocityPixels;
uniform int uSampleCount;
uniform bool uDebugView;

void main() {
    vec2 velocity = texture(uVelocityTexture, vUV).xy * uVelocityScale;
    float rawRadiusPixels = length(velocity * uResolution);

    // Remove sub-pixel movement caused by camera smoothing and physics jitter.
    // Subtracting the threshold instead of using a hard cut keeps the transition smooth.
    float radiusPixels = max(rawRadiusPixels - uMinimumVelocityPixels, 0.0);
    if (rawRadiusPixels > 0.0001) {
        velocity *= radiusPixels / rawRadiusPixels;
    }
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
    vec4 color = texture(uSceneTexture, vUV);
    float totalWeight = 1.0;
    float centerDepth = texture(uDepthTexture, vUV).r;
    // Cap the derivative contribution: at a silhouette fwidth is intentionally large,
    // but that is exactly where samples must remain separated.
    float depthThreshold = clamp(fwidth(centerDepth) * 2.0, 0.0002, 0.002);
    float centerVelocityPixels = max(length(velocity * uResolution), 0.0001);
    int samples = clamp(uSampleCount, 4, 32);
    for (int sampleIndex = 0; sampleIndex < 32; ++sampleIndex) {
        if (sampleIndex >= samples) break;
        float t = (float(sampleIndex) / float(samples - 1)) - 0.5;
        if (abs(t) < 0.0001) continue;

        vec2 sampleUV = clamp(vUV - velocity * t, vec2(0.0), vec2(1.0));
        float sampleDepth = texture(uDepthTexture, sampleUV).r;

        // Do not pull background pixels across a foreground silhouette (or vice versa).
        if (abs(sampleDepth - centerDepth) > depthThreshold * (1.0 + abs(t) * 2.0)) continue;

        // Reject pixels belonging to surfaces moving in a substantially different direction.
        vec2 sampleVelocity = texture(uVelocityTexture, sampleUV).xy * uVelocityScale;
        float sampleVelocityPixels = length(sampleVelocity * uResolution);
        if (sampleVelocityPixels > uMinimumVelocityPixels) {
            float directionAgreement = dot(velocity, sampleVelocity) /
                max(length(velocity) * length(sampleVelocity), 0.000001);
            float velocityDifference = length((sampleVelocity - velocity) * uResolution);
            if (directionAgreement < 0.25 ||
                velocityDifference > max(3.0, centerVelocityPixels * 1.5)) continue;
        }

        // Favour the current pixel to keep moving shapes readable.
        float weight = max(0.2, 1.0 - abs(t) * 1.5);
        color += texture(uSceneTexture, sampleUV) * weight;
        totalWeight += weight;
    }
    FragColor = color / totalWeight;
}
