#version 450 core

// Falling rain, composited over the lit scene.
//
// Three things distinguish this from the obvious screen-space version:
//
//  * Streaks are anchored to a WORLD direction, not to screen pixels. A screen
//    anchored overlay slides across the view whenever the camera turns, which
//    reads as rain painted on the lens rather than rain in the world - the single
//    most obvious tell in a racing game where the camera is always rotating.
//  * Three layers at different scales and speeds give parallax, so near rain
//    crosses the view faster than distant rain.
//  * Density scales with how much air is actually in front of the surface. Rain
//    is volumetric, so a wall a metre away has almost none in front of it while
//    the sky has kilometres. This is what keeps rain off the cockpit for free.

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uDepthTexture;
uniform vec2 uResolution;
uniform float uTime;
uniform float uIntensity;
uniform float uWind;
uniform float uParticleDensity;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;

const float kPi = 3.14159265359;

float Hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// One sheet of rain. `cell` is the streak spacing, `speed` the fall rate, and
// `length` how much of a cell the streak occupies.
float RainLayer(vec2 uv, float cell, float speed, float coverage, float length) {
    vec2 p = uv / cell;
    // Fall, with wind shearing the column sideways as it descends.
    p.y -= uTime * speed;
    p.x += uWind * uTime * speed * 0.30;
    vec2 id = floor(p);
    vec2 local = fract(p);

    float seed = Hash(id);
    if (seed > coverage) return 0.0;

    // Jitter each streak inside its cell, otherwise the grid is obvious.
    float offsetX = fract(seed * 7.31);
    float dx = abs(local.x - offsetX);
    // Thin streaks; written as 1 - smoothstep because GLSL leaves smoothstep
    // undefined when edge0 > edge1.
    float line = 1.0 - smoothstep(0.0, 0.055, dx);
    if (line <= 0.0) return 0.0;

    float head = fract(seed * 13.77);
    float y = fract(local.y + head);
    float streak = smoothstep(0.0, 0.12, y) * (1.0 - smoothstep(length, length + 0.25, y));
    return line * streak;
}

void main() {
    vec3 scene = texture(uSceneTexture, vUV).rgb;
    float depth = texture(uDepthTexture, vUV).r;

    // World position of whatever is under this pixel, and the view ray to it.
    vec4 clip = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    world /= world.w;
    vec3 toSurface = world.xyz - uCameraPosition;
    float surfaceDistance = depth >= 1.0 ? 5000.0 : length(toSurface);
    vec3 rayDirection = length(toSurface) > 0.0001 ? normalize(toSurface) : vec3(0.0, 0.0, 1.0);

    // Distance haze. Uses real distance rather than the nonlinear depth buffer,
    // so the falloff does not bunch up against the far plane.
    float atmosphere = clamp(surfaceDistance / 220.0, 0.0, 1.0) * uIntensity;
    vec3 stormTint = vec3(0.72, 0.79, 0.86);
    scene = mix(scene, scene * stormTint + vec3(0.015, 0.022, 0.03), atmosphere * 0.22);

    float rain = 0.0;
    if (uParticleDensity > 0.0 && uIntensity > 0.0) {
        // Direction-anchored coordinates: azimuth around the camera, and height
        // along the ray. Rotating the camera scrolls these smoothly instead of
        // dragging the whole rain field with the screen.
        float azimuth = atan(rayDirection.x, rayDirection.z) / (2.0 * kPi);
        float elevation = rayDirection.y;
        // Compress azimuth near the poles so streaks do not fan out overhead.
        float horizonScale = mix(0.35, 1.0, clamp(1.0 - abs(elevation), 0.0, 1.0));
        vec2 rainUv = vec2(azimuth * horizonScale, elevation * 0.5);

        float density = clamp(uParticleDensity, 0.0, 1.25);
        // Near layer: sparse, fast, long streaks. Far layers: dense, slow, short.
        rain += RainLayer(rainUv, 0.0075, 0.85, 0.30 * density, 0.55) * 1.00;
        rain += RainLayer(rainUv * 1.7 + 31.7, 0.0060, 0.55, 0.40 * density, 0.40) * 0.65;
        rain += RainLayer(rainUv * 2.9 + 77.1, 0.0045, 0.35, 0.50 * density, 0.30) * 0.40;
        rain *= uIntensity;

        // Volumetric falloff. Almost no air between the camera and a close
        // surface, so almost no rain in front of it. Keeps the overlay off the
        // cockpit and dashboard without a separate mask.
        rain *= clamp(surfaceDistance / 25.0, 0.0, 1.0);
    }

    // Rain is lit by whatever light is around. Without this it either vanishes
    // at night or reads as bright scratches against a dark sky.
    float sceneLuminance = dot(scene, vec3(0.2126, 0.7152, 0.0722));
    float litResponse = mix(0.30, 1.15, clamp(sceneLuminance * 1.6, 0.0, 1.0));
    vec3 rainColor = vec3(0.62, 0.74, 0.92) * rain * litResponse;

    FragColor = vec4(scene + rainColor, 1.0);
}
