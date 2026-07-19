#version 450 core

layout(location = 0) out vec2 Velocity;
in vec2 vUV;

uniform sampler2D uDepthTexture;
uniform mat4 uInverseCurrentViewProjection;
uniform mat4 uPreviousViewProjection;

void main() {
    float depth = texture(uDepthTexture, vUV).r;
    vec4 currentClip = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseCurrentViewProjection * currentClip;
    world /= max(abs(world.w), 0.00001);
    vec4 previousClip = uPreviousViewProjection * world;
    vec2 previousNdc = previousClip.xy / max(abs(previousClip.w), 0.00001);
    vec2 currentNdc = vUV * 2.0 - 1.0;
    Velocity = (currentNdc - previousNdc) * 0.5;
}
