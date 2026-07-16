#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uHdrScene;
uniform float uExposure;
uniform bool uEnableFxaa;
uniform vec2 uInverseResolution;

vec3 SampleScene(vec2 uv) {
    return texture(uHdrScene, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

vec3 SampleFxaa(vec2 uv) {
    vec3 center = SampleScene(uv);
    vec3 north = SampleScene(uv + vec2(0.0, uInverseResolution.y));
    vec3 south = SampleScene(uv - vec2(0.0, uInverseResolution.y));
    vec3 east = SampleScene(uv + vec2(uInverseResolution.x, 0.0));
    vec3 west = SampleScene(uv - vec2(uInverseResolution.x, 0.0));
    const vec3 weights = vec3(0.299, 0.587, 0.114);
    float centerLuma = dot(center, weights);
    float minLuma = min(centerLuma, min(min(dot(north, weights), dot(south, weights)),
                                        min(dot(east, weights), dot(west, weights))));
    float maxLuma = max(centerLuma, max(max(dot(north, weights), dot(south, weights)),
                                        max(dot(east, weights), dot(west, weights))));
    if (maxLuma - minLuma < 0.08) return center;
    return center * 0.5 + (north + south + east + west) * 0.125;
}

vec3 AcesFilm(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = uEnableFxaa ? SampleFxaa(vUV) : SampleScene(vUV);
    vec3 mapped = AcesFilm(hdr * uExposure);
    float dither = (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    FragColor = vec4(pow(max(mapped + dither, 0.0), vec3(1.0 / 2.2)), 1.0);
}
