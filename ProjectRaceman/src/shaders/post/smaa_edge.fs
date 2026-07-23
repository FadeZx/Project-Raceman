#version 450 core

// SMAA pass 1/3: luma edge detection.
// Ported from the reference implementation (Jimenez et al., http://www.iryoku.com/smaa/).

layout(location = 0) out vec2 FragEdges;
in vec2 vUV;

uniform sampler2D uColorTexture;
uniform vec2 uTexelSize;

// Ultra preset: catches roughly twice as many edges as the default 0.1.
const float SMAA_THRESHOLD = 0.05;
const float SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR = 2.0;

float Luma(vec2 uv) {
    return dot(texture(uColorTexture, uv).rgb, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    float L = Luma(vUV);
    float Lleft = Luma(vUV + uTexelSize * vec2(-1.0, 0.0));
    float Ltop = Luma(vUV + uTexelSize * vec2(0.0, -1.0));

    vec4 delta;
    delta.xy = abs(L - vec2(Lleft, Ltop));
    vec2 edges = step(vec2(SMAA_THRESHOLD), delta.xy);
    if (dot(edges, vec2(1.0)) == 0.0) discard;

    float Lright = Luma(vUV + uTexelSize * vec2(1.0, 0.0));
    float Lbottom = Luma(vUV + uTexelSize * vec2(0.0, 1.0));
    delta.zw = abs(L - vec2(Lright, Lbottom));
    vec2 maxDelta = max(delta.xy, delta.zw);

    float Lleftleft = Luma(vUV + uTexelSize * vec2(-2.0, 0.0));
    float Ltoptop = Luma(vUV + uTexelSize * vec2(0.0, -2.0));
    delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));
    maxDelta = max(maxDelta, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation: discard edges much weaker than their neighborhood.
    edges *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);
    FragEdges = edges;
}
