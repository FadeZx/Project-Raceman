#version 450 core

// One quad per puff, billboarded to face the camera. The quad corner comes from
// a static VBO; everything else is per-instance.
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec3 aPosition;
layout(location = 2) in float aRadius;
layout(location = 3) in vec4 aColor;
layout(location = 4) in float aRotation;

uniform mat4 uView;
uniform mat4 uViewProjection;

out vec2 vLocal;
out vec4 vColor;

void main() {
    // Camera basis straight out of the view matrix's rows: for a rigid view
    // matrix these are the world-space right and up axes of the camera, which
    // is exactly what a billboard needs and costs nothing to recover.
    vec3 cameraRight = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 cameraUp    = vec3(uView[0][1], uView[1][1], uView[2][1]);

    float s = sin(aRotation);
    float c = cos(aRotation);
    vec2 corner = vec2(aCorner.x * c - aCorner.y * s,
                       aCorner.x * s + aCorner.y * c);

    vec3 world = aPosition
               + cameraRight * (corner.x * aRadius * 2.0)
               + cameraUp    * (corner.y * aRadius * 2.0);

    vLocal = aCorner * 2.0;   // -1..1 across the quad
    vColor = aColor;
    gl_Position = uViewProjection * vec4(world, 1.0);
}
