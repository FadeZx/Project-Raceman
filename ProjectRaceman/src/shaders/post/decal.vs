#version 450 core

// Decal volume: a unit box drawn to generate fragments over the screen area the
// decal could possibly cover. The actual coverage test happens in the fragment
// shader against reconstructed world position.
//
// The shared capture cube spans [-1, 1], so it is halved here. That makes the
// decal transform's scale the full box size in world units, which is what an
// artist dragging a scale handle expects.

layout(location = 0) in vec3 aPos;

uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(aPos * 0.5, 1.0);
}
