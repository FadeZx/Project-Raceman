#version 450 core

layout(location = 0) out vec4 FragColor;

in vec2 vUvLocal;
in float vFade;

uniform float uOpacity;
uniform vec3 uTintColor;

void main() {
    // Soft edges and ends so each streak reads as a drop, not a hard rectangle.
    float edgeFade = 1.0 - smoothstep(0.30, 0.5, abs(vUvLocal.x - 0.5) * 2.0);
    float endFade = smoothstep(0.0, 0.2, vUvLocal.y) * (1.0 - smoothstep(0.7, 1.0, vUvLocal.y));
    float alpha = edgeFade * endFade * vFade * uOpacity;
    if (alpha <= 0.003) discard;
    FragColor = vec4(uTintColor, alpha);
}
