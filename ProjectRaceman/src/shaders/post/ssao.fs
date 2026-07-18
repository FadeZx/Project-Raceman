#version 450 core

layout(location = 0) out float Occlusion;
in vec2 vUV;

uniform sampler2D uDepthTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uNoiseTexture;
uniform vec3 uSamples[32];
uniform int uSampleCount;
uniform float uRadius;
uniform float uBias;
uniform vec2 uNoiseScale;
uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform mat4 uView;

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clipPosition = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPosition = uInverseProjection * clipPosition;
    return viewPosition.xyz / max(viewPosition.w, 0.000001);
}

void main() {
    float centerDepth = texture(uDepthTexture, vUV).r;
    vec4 normalSample = texture(uNormalTexture, vUV);
    if (centerDepth >= 1.0 || normalSample.a < 0.5) {
        Occlusion = 1.0;
        return;
    }

    vec3 viewPosition = ReconstructViewPosition(vUV, centerDepth);
    vec3 viewNormal = normalize(mat3(uView) * normalSample.xyz);
    vec3 randomDirection = normalize(texture(uNoiseTexture, vUV * uNoiseScale).xyz);
    vec3 tangent = randomDirection - viewNormal * dot(randomDirection, viewNormal);
    if (dot(tangent, tangent) < 0.0001) {
        tangent = abs(viewNormal.z) < 0.999
            ? normalize(cross(viewNormal, vec3(0.0, 0.0, 1.0)))
            : vec3(1.0, 0.0, 0.0);
    } else {
        tangent = normalize(tangent);
    }
    vec3 bitangent = cross(viewNormal, tangent);
    mat3 tangentBasis = mat3(tangent, bitangent, viewNormal);

    float occludedSamples = 0.0;
    int count = clamp(uSampleCount, 1, 32);
    for (int i = 0; i < 32; ++i) {
        if (i >= count) break;
        vec3 samplePosition = viewPosition + tangentBasis * uSamples[i] * uRadius;
        vec4 projected = uProjection * vec4(samplePosition, 1.0);
        vec2 sampleUv = projected.xy / projected.w * 0.5 + 0.5;
        if (sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0) continue;

        float sampleDepth = texture(uDepthTexture, sampleUv).r;
        if (sampleDepth >= 1.0) continue;
        float sampledViewZ = ReconstructViewPosition(sampleUv, sampleDepth).z;
        float rangeWeight = smoothstep(0.0, 1.0, uRadius / max(abs(viewPosition.z - sampledViewZ), 0.0001));
        occludedSamples += (sampledViewZ >= samplePosition.z + uBias ? 1.0 : 0.0) * rangeWeight;
    }

    Occlusion = 1.0 - occludedSamples / float(count);
}
