#version 450 core

in vec3 vWorldPosition;
uniform vec3 uLightPosition;
uniform float uLightRange;

void main() {
    gl_FragDepth = clamp(length(vWorldPosition - uLightPosition) / max(uLightRange, 0.001), 0.0, 1.0);
}
