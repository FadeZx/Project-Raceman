#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uDepthTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uMaterialTexture;
uniform mat4 uProjection;
uniform mat4 uInverseProjection;
uniform mat4 uView;
uniform vec2 uTexelSize;
uniform float uIntensity;
uniform float uMaxDistance;
uniform float uThickness;
uniform int uMaxSteps;
uniform bool uDebugView;

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPosition = uInverseProjection * clip;
    return viewPosition.xyz / max(abs(viewPosition.w), 0.00001);
}

vec2 ProjectToUv(vec3 viewPosition) {
    vec4 clip = uProjection * vec4(viewPosition, 1.0);
    if (clip.w <= 0.0001) return vec2(-1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

vec3 SampleRoughReflection(vec2 uv, float roughness) {
    vec2 radius = uTexelSize * (1.0 + roughness * 6.0);
    vec3 color = texture(uSceneTexture, uv).rgb * 0.40;
    color += texture(uSceneTexture, clamp(uv + vec2(radius.x, 0.0), vec2(0.0), vec2(1.0))).rgb * 0.15;
    color += texture(uSceneTexture, clamp(uv - vec2(radius.x, 0.0), vec2(0.0), vec2(1.0))).rgb * 0.15;
    color += texture(uSceneTexture, clamp(uv + vec2(0.0, radius.y), vec2(0.0), vec2(1.0))).rgb * 0.15;
    color += texture(uSceneTexture, clamp(uv - vec2(0.0, radius.y), vec2(0.0), vec2(1.0))).rgb * 0.15;
    return color;
}

void main() {
    vec4 scene = texture(uSceneTexture, vUV);
    float depth = texture(uDepthTexture, vUV).r;
    vec4 normalSample = texture(uNormalTexture, vUV);
    vec4 material = texture(uMaterialTexture, vUV);
    float metallic = clamp(material.r, 0.0, 1.0);
    float roughness = clamp(material.g, 0.045, 1.0);
    float clearCoat = clamp(material.b, 0.0, 1.0);
    float materialValid = material.a;

    if (depth >= 0.999999 || normalSample.a < 0.5 || materialValid < 0.5 || roughness >= 0.92) {
        FragColor = uDebugView ? vec4(0.0, 0.0, 0.0, 1.0) : scene;
        return;
    }

    vec3 viewPosition = ReconstructViewPosition(vUV, depth);
    vec3 viewNormal = normalize(mat3(uView) * normalSample.xyz);
    vec3 incident = normalize(viewPosition);
    vec3 reflectionDirection = normalize(reflect(incident, viewNormal));
    vec3 rayPosition = viewPosition + viewNormal * max(0.03, uThickness * 0.25);

    int stepCount = clamp(uMaxSteps, 8, 96);
    float stepLength = clamp(uThickness * 0.5, 0.05, 0.5);
    float maximumStepLength = max(uMaxDistance / float(stepCount) * 2.0, stepLength);
    bool hit = false;
    vec2 hitUv = vec2(0.0);
    float travelled = 0.0;

    for (int stepIndex = 0; stepIndex < 96; ++stepIndex) {
        if (stepIndex >= stepCount) break;
        float currentStepLength = stepLength;
        travelled += stepLength;
        rayPosition += reflectionDirection * stepLength;
        stepLength = min(stepLength * 1.10, maximumStepLength);
        if (travelled > uMaxDistance || rayPosition.z >= -0.01) break;

        vec2 rayUv = ProjectToUv(rayPosition);
        if (any(lessThanEqual(rayUv, vec2(0.001))) || any(greaterThanEqual(rayUv, vec2(0.999)))) break;
        float sceneDepth = texture(uDepthTexture, rayUv).r;
        if (sceneDepth >= 0.999999) continue;
        vec3 scenePosition = ReconstructViewPosition(rayUv, sceneDepth);
        float depthDelta = (-rayPosition.z) - (-scenePosition.z);
        float adaptiveThickness = max(uThickness * (1.0 + (-rayPosition.z) * 0.01), currentStepLength * 1.25);
        if (depthDelta >= 0.0 && depthDelta <= adaptiveThickness) {
            hit = true;
            hitUv = rayUv;
            break;
        }
    }

    float edgeDistance = min(min(hitUv.x, 1.0 - hitUv.x), min(hitUv.y, 1.0 - hitUv.y));
    float edgeFade = hit ? smoothstep(0.0, 0.08, edgeDistance) : 0.0;
    float distanceFade = hit ? clamp(1.0 - travelled / max(uMaxDistance, 0.001), 0.0, 1.0) : 0.0;
    float roughnessFade = pow(1.0 - roughness, 2.0);
    float nDotV = max(dot(viewNormal, -incident), 0.0);
    float baseReflectance = mix(0.04, 1.0, metallic);
    float fresnel = baseReflectance + (1.0 - baseReflectance) * pow(1.0 - nDotV, 5.0);
    float reflectionWeight = edgeFade * distanceFade * roughnessFade *
        max(fresnel, clearCoat * 0.04) * max(uIntensity, 0.0);

    if (uDebugView) {
        FragColor = vec4(hit ? edgeFade : 0.0, reflectionWeight, roughnessFade, 1.0);
        return;
    }

    vec3 reflectedColor = hit ? SampleRoughReflection(hitUv, roughness) : vec3(0.0);
    // SSR is additive over the existing skybox IBL. A miss therefore retains
    // the global environment fallback and can never darken the material.
    FragColor = vec4(scene.rgb + reflectedColor * reflectionWeight, scene.a);
}
