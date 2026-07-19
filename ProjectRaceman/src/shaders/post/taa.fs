#version 450 core

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragSurfaceHistory;
in vec2 vUV;

uniform sampler2D uCurrentTexture;
uniform sampler2D uHistoryTexture;
uniform sampler2D uVelocityTexture;
uniform sampler2D uDepthTexture;
uniform sampler2D uHistorySurfaceTexture;
uniform sampler2D uNormalTexture;
uniform mat4 uInverseProjection;
uniform vec2 uTexelSize;
uniform vec2 uCurrentJitterUv;
uniform vec2 uPreviousJitterUv;
uniform float uFeedback;
uniform float uSharpness;
uniform bool uDebugView;

vec3 RGBToYCoCg(vec3 color) {
    float y = dot(color, vec3(0.25, 0.5, 0.25));
    float co = color.r - color.b;
    float cg = color.g - 0.5 * (color.r + color.b);
    return vec3(y, co, cg);
}

vec3 YCoCgToRGB(vec3 color) {
    float middle = color.x - color.z * 0.5;
    return vec3(middle + color.y * 0.5, color.x + color.z * 0.5, middle - color.y * 0.5);
}

vec3 CompressHdr(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color / (1.0 + max(luminance, 0.0));
}

vec3 ExpandHdr(vec3 color) {
    float compressedLuminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color / max(1.0 - compressedLuminance, 0.0001);
}

vec2 EncodeOctahedralNormal(vec3 normalValue) {
    vec3 normal = normalValue / max(abs(normalValue.x) + abs(normalValue.y) + abs(normalValue.z), 0.0001);
    vec2 encoded = normal.xy;
    if (normal.z < 0.0) {
        encoded = (1.0 - abs(encoded.yx)) * vec2(
            encoded.x >= 0.0 ? 1.0 : -1.0,
            encoded.y >= 0.0 ? 1.0 : -1.0);
    }
    return encoded;
}

vec3 DecodeOctahedralNormal(vec2 encoded) {
    vec3 normal = vec3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
    float fold = clamp(-normal.z, 0.0, 1.0);
    normal.xy += vec2(normal.x >= 0.0 ? -fold : fold,
        normal.y >= 0.0 ? -fold : fold);
    return normalize(normal);
}

float CatmullRomWeight(float distanceValue) {
    float x = abs(distanceValue);
    if (x <= 1.0) return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0) return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

vec3 SampleHistoryCatmullRom(vec2 uv) {
    vec2 texelPosition = uv / uTexelSize - 0.5;
    vec2 basePosition = floor(texelPosition);
    vec2 fractionValue = fract(texelPosition);
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    for (int y = -1; y <= 2; ++y) {
        float weightY = CatmullRomWeight(float(y) - fractionValue.y);
        for (int x = -1; x <= 2; ++x) {
            float weight = weightY * CatmullRomWeight(float(x) - fractionValue.x);
            vec2 sampleUV = (basePosition + vec2(x, y) + 0.5) * uTexelSize;
            result += texture(uHistoryTexture, clamp(sampleUV, vec2(0.0), vec2(1.0))).rgb * weight;
            totalWeight += weight;
        }
    }
    return result / max(totalWeight, 0.0001);
}

float ViewDepth(float deviceDepth) {
    vec4 viewPosition = uInverseProjection * vec4(0.0, 0.0, deviceDepth * 2.0 - 1.0, 1.0);
    return abs(viewPosition.z / max(abs(viewPosition.w), 0.00001));
}

void main() {
    // Resolve from the jittered render grid into stable, unjittered output pixels.
    vec2 currentUV = clamp(vUV + uCurrentJitterUv, vec2(0.0), vec2(1.0));
    vec4 current = texture(uCurrentTexture, currentUV);
    float currentDepth = texture(uDepthTexture, currentUV).r;
    vec3 currentNormal = texture(uNormalTexture, currentUV).xyz;
    float currentViewDepth = ViewDepth(currentDepth);
    float currentNormalLength = length(currentNormal);
    vec2 currentEncodedNormal = currentNormalLength > 0.5
        ? EncodeOctahedralNormal(currentNormal / currentNormalLength)
        : vec2(0.0);
    const float historyAgeStep = 1.0 / 32.0;
    vec4 currentSurface = vec4(log2(1.0 + currentViewDepth), currentEncodedNormal, historyAgeStep);
    FragSurfaceHistory = currentSurface;

    // A reset frame must never sample uninitialized history.
    if (uFeedback <= 0.0001) {
        FragColor = current;
        return;
    }

    // Dilate motion vectors using the nearest surface. This closes the zero-velocity
    // cracks that otherwise appear around moving car silhouettes.
    vec2 velocity = texture(uVelocityTexture, currentUV).xy;
    float nearestDepth = currentDepth;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 sampleUV = clamp(currentUV + vec2(x, y) * uTexelSize, vec2(0.0), vec2(1.0));
            float sampleDepth = texture(uDepthTexture, sampleUV).r;
            if (sampleDepth < nearestDepth) {
                nearestDepth = sampleDepth;
                velocity = texture(uVelocityTexture, sampleUV).xy;
            }
        }
    }

    // Velocity maps current jittered coordinates to previous jittered coordinates.
    // History is stored on an unjittered grid, so remove the previous frame offset.
    vec2 historyUV = currentUV - velocity - uPreviousJitterUv;
    bool historyOnScreen = all(greaterThanEqual(historyUV, vec2(0.0))) &&
        all(lessThanEqual(historyUV, vec2(1.0)));

    vec3 neighborhoodMin = vec3(1e20);
    vec3 neighborhoodMax = vec3(-1e20);
    vec3 firstMoment = vec3(0.0);
    vec3 secondMoment = vec3(0.0);
    vec3 crossSum = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 sampleUV = clamp(currentUV + vec2(x, y) * uTexelSize, vec2(0.0), vec2(1.0));
            vec3 sampleColor = texture(uCurrentTexture, sampleUV).rgb;
            vec3 sampleYCoCg = RGBToYCoCg(CompressHdr(sampleColor));
            neighborhoodMin = min(neighborhoodMin, sampleYCoCg);
            neighborhoodMax = max(neighborhoodMax, sampleYCoCg);
            firstMoment += sampleYCoCg;
            secondMoment += sampleYCoCg * sampleYCoCg;
            if (x == 0 || y == 0) crossSum += sampleColor;
        }
    }

    vec3 mean = firstMoment / 9.0;
    vec3 variance = max(secondMoment / 9.0 - mean * mean, vec3(0.0));
    vec3 standardDeviation = sqrt(variance);
    float motionPixels = length(velocity / max(uTexelSize, vec2(0.000001)));
    float motionAmount = clamp(motionPixels / 24.0, 0.0, 1.0);

    vec3 historyRGB = historyOnScreen ? SampleHistoryCatmullRom(historyUV) : current.rgb;
    vec3 historyYCoCg = RGBToYCoCg(CompressHdr(historyRGB));

    // Variance clipping is tighter during motion and prevents old high-contrast
    // samples from trailing behind the vehicle.
    float varianceGamma = mix(1.5, 0.75, motionAmount);
    vec3 varianceMin = mean - standardDeviation * varianceGamma;
    vec3 varianceMax = mean + standardDeviation * varianceGamma;
    vec3 clipMin = max(neighborhoodMin, varianceMin);
    vec3 clipMax = min(neighborhoodMax, varianceMax);
    historyYCoCg = clamp(historyYCoCg, clipMin, clipMax);
    vec3 clippedHistory = max(ExpandHdr(YCoCgToRGB(historyYCoCg)), vec3(0.0));

    vec4 historySurface = historyOnScreen ? texture(uHistorySurfaceTexture, historyUV) : currentSurface;
    float historyViewDepth = exp2(historySurface.r) - 1.0;
    float depthTolerance = max(0.05, currentViewDepth * 0.02);
    float depthDifference = abs(currentViewDepth - historyViewDepth);
    float depthConfidence = 1.0 - smoothstep(depthTolerance, depthTolerance * 2.0, depthDifference);

    float normalConfidence = 1.0;
    float farViewDepth = ViewDepth(1.0);
    if (currentNormalLength > 0.5 && historyViewDepth < farViewDepth * 0.999) {
        vec3 historyNormal = DecodeOctahedralNormal(historySurface.gb);
        float normalSimilarity = dot(currentNormal / currentNormalLength, historyNormal);
        normalConfidence = smoothstep(0.45, 0.90, normalSimilarity);
    }

    // Rapid color changes act as a lightweight reactive mask for reflections,
    // transparencies and other pixels that cannot safely keep long history.
    float relativeColorDifference = length(current.rgb - clippedHistory) /
        max(0.25, dot(current.rgb, vec3(0.2126, 0.7152, 0.0722)) + 0.25);
    float reactiveAmount = clamp(relativeColorDifference * 1.5, 0.0, 1.0);

    float feedback = historyOnScreen ? uFeedback : 0.0;
    feedback *= depthConfidence;
    feedback *= normalConfidence;
    feedback *= mix(1.0, 0.35, motionAmount);
    feedback *= mix(1.0, 0.10, reactiveAmount);

    // Build confidence over the first 32 accepted frames instead of applying a
    // large global history weight immediately after exposure or disocclusion.
    float historySamples = max(historySurface.a / historyAgeStep, 1.0);
    float convergenceFeedback = historySamples / (historySamples + 1.0);
    feedback = min(feedback, convergenceFeedback);

    float retentionConfidence = historyOnScreen
        ? depthConfidence * normalConfidence * (1.0 - reactiveAmount)
        : 0.0;
    float continuedAge = min(historySurface.a + historyAgeStep, 1.0);
    float newHistoryAge = mix(historyAgeStep, continuedAge, retentionConfidence);
    FragSurfaceHistory = vec4(log2(1.0 + currentViewDepth), currentEncodedNormal, newHistoryAge);

    vec3 resolved = mix(current.rgb, clippedHistory, clamp(feedback, 0.0, 0.98));
    vec3 crossAverage = crossSum / 5.0;
    resolved += (current.rgb - crossAverage) * uSharpness;
    resolved = max(resolved, vec3(0.0));

    if (uDebugView) {
        FragColor = vec4(max(1.0 - depthConfidence, 1.0 - normalConfidence), feedback, motionAmount, 1.0);
    } else {
        FragColor = vec4(resolved, current.a);
    }
}
