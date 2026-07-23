#version 450 core

// SMAA pass 3/3: neighborhood blending.
// Ported from the reference implementation (Jimenez et al., http://www.iryoku.com/smaa/).

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uColorTexture; // linear filtering required
uniform sampler2D uBlendTexture;
uniform vec4 uRtMetrics; // 1/w, 1/h, w, h

void main() {
    vec4 offset = uRtMetrics.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + vUV.xyxy;

    vec4 a;
    a.x = texture(uBlendTexture, offset.xy).a;  // Right
    a.y = texture(uBlendTexture, offset.zw).g;  // Below
    a.wz = texture(uBlendTexture, vUV).xz;      // Above / Left

    if (dot(a, vec4(1.0)) < 1e-5) {
        FragColor = textureLod(uColorTexture, vUV, 0.0);
        return;
    }

    bool horizontal = max(a.x, a.z) > max(a.y, a.w);
    vec4 blendingOffset = horizontal ? vec4(a.x, 0.0, a.z, 0.0) : vec4(0.0, a.y, 0.0, a.w);
    vec2 blendingWeight = horizontal ? a.xz : a.yw;
    blendingWeight /= dot(blendingWeight, vec2(1.0));

    vec4 blendingCoord = blendingOffset * vec4(uRtMetrics.xy, -uRtMetrics.xy) + vUV.xyxy;
    vec4 color = blendingWeight.x * textureLod(uColorTexture, blendingCoord.xy, 0.0);
    color += blendingWeight.y * textureLod(uColorTexture, blendingCoord.zw, 0.0);
    FragColor = color;
}
