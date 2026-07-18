#version 450 core

layout(location = 0) out vec2 Velocity;

in vec4 vCurrentClip;
in vec4 vPreviousClip;

void main() {
    vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 0.00001);
    vec2 previousNdc = vPreviousClip.xy / max(abs(vPreviousClip.w), 0.00001);
    Velocity = (currentNdc - previousNdc) * 0.5;
}
