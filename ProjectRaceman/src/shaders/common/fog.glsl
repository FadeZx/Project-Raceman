// Analytic distance and height fog.
//
// Applied in-shader rather than as a depth-buffer post pass. The renderer is
// forward, so this is both cheaper (no extra fullscreen pass or bandwidth) and
// more correct: blended surfaces do not write depth, and a post pass would
// therefore leave every transparent surface — glass, windows, spray — unfogged.
//
// Include with:  #include <common/fog.glsl>
// Callers own the #version directive; the loader strips any found here.
//
// Costs nothing when disabled: uFogMode == 0 returns the input colour before
// any of the maths below runs.

#ifndef RACEMAN_COMMON_FOG
#define RACEMAN_COMMON_FOG

// 0 = off, 1 = linear, 2 = exponential height. Matches raceman::FogMode.
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeightFalloff;
uniform float uFogBaseHeight;
uniform float uFogStartDistance;
uniform float uFogMaxOpacity;
uniform float uFogLinearStart;
uniform float uFogLinearEnd;
uniform vec3 uFogSunDirection;
uniform vec3 uFogSunColor;
uniform float uFogSunIntensity;
uniform float uFogSunExponent;
uniform bool uFogDebugView;
// Sky-matched inscatter. uFogSkyMap is the prefiltered environment cubemap; the
// caller binds it and clears uFogUseSkyColor when no environment is available.
uniform bool uFogUseSkyColor;
uniform samplerCube uFogSkyMap;
uniform float uFogSkyMipLevel;

// Fraction of the surface's own radiance that survives the trip to the camera.
// 1.0 = perfectly clear, 0.0 = fully occluded by fog.
float FogTransmittance(vec3 cameraPosition, vec3 worldPosition) {
    if (uFogMode == 0) return 1.0;

    vec3 delta = worldPosition - cameraPosition;
    float rayLength = max(length(delta) - uFogStartDistance, 0.0);
    if (rayLength <= 0.0) return 1.0;

    float transmittance;
    if (uFogMode == 1) {
        float fogStart = uFogLinearStart;
        float fogEnd = max(uFogLinearEnd, fogStart + 0.001);
        float distance = length(delta);
        transmittance = clamp((fogEnd - distance) / (fogEnd - fogStart), 0.0, 1.0);
    } else {
        // Density at height y is uFogDensity * exp(-falloff * (y - baseHeight)).
        // Integrating that along the view ray has a closed form, so there is no
        // marching and no dependence on scene depth complexity.
        float baseDensity = uFogDensity *
            exp(-uFogHeightFalloff * (cameraPosition.y - uFogBaseHeight));

        // The integral of exp(-falloff * y) over the ray. As the ray approaches
        // horizontal this tends to 1 (constant density); guard it, or a level
        // camera — which is most of a racing game — divides by zero.
        float t = uFogHeightFalloff * delta.y;
        float integral = abs(t) > 1e-4 ? (1.0 - exp(-t)) / t : 1.0;

        float opticalDepth = baseDensity * rayLength * max(integral, 0.0);
        transmittance = exp(-opticalDepth);
    }

    // Reinterpret max opacity as a floor on transmittance, so lowering it keeps
    // distant silhouettes readable instead of flattening them into the fog.
    return clamp(mix(1.0, transmittance, clamp(uFogMaxOpacity, 0.0, 1.0)), 0.0, 1.0);
}

// Light scattered into the view ray. Two lobes: a uniform ambient colour, and a
// sun-tinted one tight around the light direction.
vec3 FogInscatter(vec3 viewDirection) {
    // Aerial perspective: a distant surface fades toward the sky behind it, not
    // toward a single authored colour. A blurred mip is what makes this read as
    // ambient sky rather than a mirror of whatever cloud is in that direction.
    vec3 baseColor = uFogUseSkyColor
        ? textureLod(uFogSkyMap, -viewDirection, uFogSkyMipLevel).rgb
        : uFogColor;

    if (uFogSunIntensity <= 0.0) return baseColor;
    // viewDirection points from the surface toward the camera, so the sun lobe
    // peaks when the camera looks back down the light's direction of travel.
    float sunAmount = pow(max(dot(-viewDirection, uFogSunDirection), 0.0),
                          max(uFogSunExponent, 1.0));
    return mix(baseColor, uFogSunColor, clamp(sunAmount * uFogSunIntensity, 0.0, 1.0));
}

vec3 ApplyFog(vec3 color, vec3 cameraPosition, vec3 worldPosition, vec3 viewDirection) {
    if (uFogMode == 0) return color;
    float transmittance = FogTransmittance(cameraPosition, worldPosition);
    return color * transmittance + FogInscatter(viewDirection) * (1.0 - transmittance);
}

#endif
