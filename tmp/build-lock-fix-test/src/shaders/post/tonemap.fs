#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uHdrScene;
uniform sampler2D uBloomTexture;
uniform sampler2D uSsaoTexture;
uniform float uExposure;
// 0 = SDR sRGB, 1 = HDR linear scRGB, 2 = SDR preview of HDR output.
uniform int uOutputMode;
uniform float uHdrPaperWhiteNits;
uniform float uHdrPeakBrightnessNits;
uniform bool uEnableFxaa;
uniform bool uEnableBloom;
uniform bool uDebugSsao;
uniform float uBloomIntensity;
uniform float uSsaoIntensity;
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

vec3 EncodeSrgb(vec3 linearColor) {
    vec3 low = linearColor * 12.92;
    vec3 high = 1.055 * pow(max(linearColor, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linearColor, vec3(0.0031308)));
}

vec3 ToScRgb(vec3 sceneLinear) {
    float paperWhite = clamp(uHdrPaperWhiteNits, 80.0, 500.0);
    float peakBrightness = clamp(uHdrPeakBrightnessNits, paperWhite, 4000.0);
    float peakRatio = peakBrightness / paperWhite;
    // A smooth shoulder preserves values above SDR white while asymptotically
    // limiting the result to the configured display peak.
    vec3 relativeNits = peakRatio * (vec3(1.0) - exp(-max(sceneLinear, vec3(0.0)) / peakRatio));
    return relativeNits * (paperWhite / 80.0);
}

vec3 ToSdr(vec3 sceneLinear, vec2 pixelPosition) {
    vec3 mapped = AcesFilm(sceneLinear);
    float dither = (fract(sin(dot(pixelPosition, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    return EncodeSrgb(max(mapped + dither, 0.0));
}

void main() {
    if (uDebugSsao) {
        float rawOcclusion = clamp(texture(uSsaoTexture, vUV).r, 0.0, 1.0);
        float occlusion = clamp(1.0 - (1.0 - rawOcclusion) * uSsaoIntensity, 0.0, 1.0);
        vec3 debugColor = vec3(occlusion);
        FragColor = vec4(uOutputMode == 1 ? ToScRgb(debugColor) : EncodeSrgb(debugColor), 1.0);
        return;
    }
    vec3 hdr = uEnableFxaa ? SampleFxaa(vUV) : SampleScene(vUV);
    if (uEnableBloom) hdr += texture(uBloomTexture, vUV).rgb * uBloomIntensity;
    vec3 exposed = hdr * uExposure;
    FragColor = vec4(uOutputMode == 1 ? ToScRgb(exposed) : ToSdr(exposed, gl_FragCoord.xy), 1.0);
}
