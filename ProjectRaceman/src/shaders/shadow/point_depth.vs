#version 450 core

layout(location = 0) in vec3 aPos;
uniform mat4 uLightMVP;
uniform mat4 uModel;
out vec3 vWorldPosition;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPosition = world.xyz;
    gl_Position = uLightMVP * vec4(aPos, 1.0);
}
