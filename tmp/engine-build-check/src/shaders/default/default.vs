#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec2 uUvTiling;
uniform vec2 uUvOffset;

out vec2 vUV;
out vec3 vWorldPosition;
out vec3 vWorldNormal;

void main() {
    vUV = aUV * uUvTiling + uUvOffset;
    vec4 worldPosition = uModel * vec4(aPos, 1.0);
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
