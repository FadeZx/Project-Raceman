#version 450 core

layout(location = 0) out float Occlusion;
in vec2 vUV;

uniform sampler2D uSsaoTexture;
uniform sampler2D uDepthTexture;
uniform sampler2D uNormalTexture;
uniform vec2 uTexelSize;
uniform mat4 uInverseProjection;

float ReconstructViewDepth(vec2 uv, float depth) {
    vec4 clipPosition = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPosition = uInverseProjection * clipPosition;
    return viewPosition.z / viewPosition.w;
}

void main() {
    float centerDepthSample = texture(uDepthTexture, vUV).r;
    vec4 centerNormalSample = texture(uNormalTexture, vUV);
    if (centerDepthSample >= 1.0 || centerNormalSample.a < 0.5) {
        Occlusion = 1.0;
        return;
    }

    float centerDepth = ReconstructViewDepth(vUV, centerDepthSample);
    vec3 centerNormal = normalize(centerNormalSample.xyz);
    float weightedOcclusion = 0.0;
    float totalWeight = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUv = clamp(vUV + vec2(x, y) * uTexelSize, vec2(0.0), vec2(1.0));
            float sampleDepthValue = texture(uDepthTexture, sampleUv).r;
            vec4 sampleNormalValue = texture(uNormalTexture, sampleUv);
            if (sampleDepthValue >= 1.0 || sampleNormalValue.a < 0.5) continue;

            float sampleDepth = ReconstructViewDepth(sampleUv, sampleDepthValue);
            vec3 sampleNormal = normalize(sampleNormalValue.xyz);
            float spatialWeight = exp(-float(x * x + y * y) / 8.0);
            float depthWeight = exp(-abs(centerDepth - sampleDepth) * 4.0);
            float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), 16.0);
            float weight = spatialWeight * depthWeight * normalWeight;
            weightedOcclusion += texture(uSsaoTexture, sampleUv).r * weight;
            totalWeight += weight;
        }
    }

    Occlusion = totalWeight > 0.0001
        ? weightedOcclusion / totalWeight
        : texture(uSsaoTexture, vUV).r;
}
