#version 450 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uCurrentMVP;
uniform mat4 uPreviousMVP;

out vec4 vCurrentClip;
out vec4 vPreviousClip;

void main() {
    vCurrentClip = uCurrentMVP * vec4(aPosition, 1.0);
    vPreviousClip = uPreviousMVP * vec4(aPosition, 1.0);
    gl_Position = vCurrentClip;
}
