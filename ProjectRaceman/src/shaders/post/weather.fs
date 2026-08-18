#version 450 core

// Atmospheric haze for bad weather. The rain drops themselves are real,
// depth-tested world geometry (see rain_particles.vs/.fs, drawn during the
// main scene pass) rather than anything screen-space - a screen overlay has
// no way to be occluded by track geometry or to react to the camera actually
// moving instead of just turning, which is exactly what a racing camera does
// most of the time. This pass is left with only the part a full-screen
// effect is legitimately good at: distant air getting hazier as it fills
// with more water.

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uSceneTexture;
uniform sampler2D uDepthTexture;
uniform vec2 uResolution;
uniform float uIntensity;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;

void main() {
    vec3 scene = texture(uSceneTexture, vUV).rgb;
    float depth = texture(uDepthTexture, vUV).r;

    // World position of whatever is under this pixel, and its distance from
    // the camera.
    vec4 clip = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    world /= world.w;
    float surfaceDistance = depth >= 1.0 ? 5000.0 : length(world.xyz - uCameraPosition);

    // Distance haze. Uses real distance rather than the nonlinear depth buffer,
    // so the falloff does not bunch up against the far plane.
    float atmosphere = clamp(surfaceDistance / 220.0, 0.0, 1.0) * uIntensity;
    vec3 stormTint = vec3(0.72, 0.79, 0.86);
    scene = mix(scene, scene * stormTint + vec3(0.015, 0.022, 0.03), atmosphere * 0.22);

    FragColor = vec4(scene, 1.0);
}
