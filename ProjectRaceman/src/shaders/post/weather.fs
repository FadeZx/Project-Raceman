#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uDepthTexture;
uniform vec2 uResolution;
uniform float uTime;
uniform float uIntensity;
uniform float uWind;
uniform float uParticleDensity;

float Hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main() {
    vec3 scene = texture(uSceneTexture, vUV).rgb;
    float depth = texture(uDepthTexture, vUV).r;
    float atmosphere = smoothstep(0.35, 1.0, depth) * uIntensity;
    vec3 stormTint = vec3(0.72, 0.79, 0.86);
    scene = mix(scene, scene * stormTint + vec3(0.015, 0.022, 0.03), atmosphere * 0.22);

    float rain = 0.0;
    if (uParticleDensity > 0.0) {
        float cellSize = mix(34.0, 16.0, clamp(uParticleDensity / 1.25, 0.0, 1.0));
        vec2 pixel = vUV * uResolution;
        vec2 falling = pixel + vec2(-uWind * uTime * 90.0, uTime * 520.0);
        vec2 cell = floor(falling / cellSize);
        vec2 local = fract(falling / cellSize);
        float seed = Hash(cell);
        float x = fract(seed + local.y * uWind * 0.12);
        float line = smoothstep(0.055, 0.0, abs(local.x - x));
        float segment = smoothstep(0.05, 0.22, local.y) * (1.0 - smoothstep(0.72, 0.98, local.y));
        float particleVisible = step(1.0 - 0.48 * clamp(uParticleDensity, 0.0, 1.25), seed);
        rain = line * segment * particleVisible * uIntensity;
    }
    vec3 rainColor = vec3(0.65, 0.78, 0.92) * rain * mix(0.35, 1.0, depth);
    FragColor = vec4(scene + rainColor, 1.0);
}
