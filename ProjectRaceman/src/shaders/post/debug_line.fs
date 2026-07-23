#version 450 core
layout(location = 0) out vec4 FragColor;

uniform vec4 uColor;
uniform sampler2D uDepthTexture;
uniform vec2 uScreenSize;
uniform bool uUseDepthTest;

void main() {
    if (uUseDepthTest) {
        float sceneDepth = texture(uDepthTexture, gl_FragCoord.xy / uScreenSize).r;
        if (gl_FragCoord.z > sceneDepth) {
            discard;
        }
    }
    FragColor = uColor;
}
