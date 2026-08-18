#version 450 core

// Real rain: each instance is one drop, given an actual world position rather
// than a screen-space pattern. That is what makes it react correctly to the
// camera moving through the world (true parallax - near drops rush past,
// far ones barely shift) instead of only to the camera turning, which is all
// a direction-only overlay can ever show.
//
// Drops live in a box of size (uAreaSize, uHeight, uAreaSize) that follows the
// camera in discrete steps of its own size. Snapping in whole-box steps
// (instead of re-centring every frame) is what keeps drops fixed in world
// space between snaps, so driving through them looks like driving through
// rain rather than dragging a rain-shaped box along with the car. Falling and
// wind drift are then applied as a wrapped local offset inside that box, so
// no per-particle simulation state has to be kept between frames.

layout(location = 0) in vec2 aCorner;

uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform float uTime;
uniform float uAreaSize;
uniform float uHeight;
uniform float uFallSpeed;
uniform vec2 uWind;
uniform float uStreakLength;
uniform float uStreakWidth;

out vec2 vUvLocal;
out float vFade;

float Hash(float n) {
    return fract(sin(n) * 43758.5453123);
}

vec3 Hash3(float n) {
    return vec3(Hash(n * 12.9898), Hash(n * 78.2330 + 11.13), Hash(n * 39.1901 + 23.71));
}

void main() {
    vec3 boxSize = vec3(uAreaSize, uHeight, uAreaSize);
    // Discrete snap, not a continuous follow: this is what makes drops stay
    // put in world space as the camera drives through them. A continuous
    // follow (cellOrigin = cameraPosition every frame) would cancel out
    // translation entirely and reproduce the screen-locked look this exists
    // to fix.
    vec3 cellOrigin = floor(uCameraPosition / boxSize + 0.5) * boxSize;

    vec3 rand = Hash3(float(gl_InstanceID) + 1.0);
    vec3 localHome = (rand - 0.5) * boxSize;

    vec3 localPos = localHome;
    localPos.y -= uTime * uFallSpeed;
    localPos.xz -= uTime * uWind;
    localPos = mod(localPos + boxSize * 0.5, boxSize) - boxSize * 0.5;

    vec3 dropCenter = cellOrigin + localPos;

    vec3 toCamera = uCameraPosition - dropCenter;
    float distanceToCamera = length(toCamera);
    vec3 viewDir = distanceToCamera > 0.0001 ? toCamera / distanceToCamera : vec3(0.0, 0.0, 1.0);

    // Stretched along the drop's actual velocity - wind sideways, gravity
    // down - with the ribbon's width kept facing the camera at every angle.
    // This is a trail billboard, not a flat camera-aligned card.
    vec3 velocity = vec3(uWind.x, -uFallSpeed, uWind.y);
    vec3 lengthAxis = normalize(velocity);
    vec3 widthAxis = cross(viewDir, lengthAxis);
    float widthAxisLen = length(widthAxis);
    widthAxis = widthAxisLen > 0.0001 ? widthAxis / widthAxisLen : vec3(1.0, 0.0, 0.0);

    vec3 worldPos = dropCenter
        + lengthAxis * (aCorner.y * uStreakLength)
        + widthAxis * (aCorner.x * uStreakWidth);

    // Fade drops out near the box boundary rather than let them pop, and
    // fade the ones right at the lens so cockpit views are not smothered.
    float boundaryFade = 1.0 - smoothstep(uAreaSize * 0.32, uAreaSize * 0.5, distanceToCamera);
    float nearFade = smoothstep(0.35, 1.6, distanceToCamera);
    vFade = boundaryFade * nearFade;

    vUvLocal = aCorner + 0.5;
    gl_Position = uViewProjection * vec4(worldPos, 1.0);
}
