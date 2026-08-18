// Surface wetness: what water does to a material before it is lit.
//
// Water behaves differently depending on the surface it lands on and how that
// surface is angled, so this resolves three regimes rather than one:
//
//   Wet film   - everywhere. Darkens albedo and tightens the specular lobe.
//                A vertical wall in rain is wet even though nothing pools on it.
//   Puddles    - near-horizontal surfaces only. Standing water with its own level
//                normal, which is what reflects trackside objects.
//   Droplets   - sloped and curved surfaces. Water cannot pool, so it beads and
//                runs off. This is what car bodywork gets, and it is why paint
//                never shows puddles no matter how wet the scene is.
//
// The regime blend is automatic from the surface normal. Materials then scale
// how strongly they respond and whether they can pool at all.
//
// The reduced roughness is written to MaterialBuffer, so the existing SSR pass
// reflects on wet road with no changes of its own.

#ifndef RACEMAN_COMMON_WETNESS
#define RACEMAN_COMMON_WETNESS

uniform float uWetness;             // 0 = dry, 1 = fully saturated
uniform float uWetnessPuddleAmount; // how much of a poolable area becomes water
uniform float uWetnessPuddleScale;  // puddle size, world units
uniform float uWetnessRippleStrength;
uniform float uWetnessRippleSpeed;
uniform float uWetnessDropletScale;    // bead size, world units
uniform float uWetnessDropletStrength; // how much beads distort the surface
uniform float uWetnessRunoffSpeed;     // how fast beads slide downhill
uniform float uWetnessTime;
uniform bool uWetnessDebugView;

// Per-material response, uploaded per draw.
uniform float uMaterialWetnessResponse; // 0 = stays dry (sealed, sheltered)
uniform float uMaterialPuddleAffinity;  // 0 = never pools (paint, glass, foliage)
// Multiply the global puddle/droplet scale uniforms above. Those sliders are
// sized for whatever surface they were last tuned against - a puddle cell
// that reads as normal spacing on a track becomes one giant pool covering an
// entire car hood. 1 = use the scene scale as authored.
uniform float uMaterialPuddleScale;
uniform float uMaterialDropletScale;

// Shelter volumes: regions the rain cannot reach. A garage floor is the same
// asphalt material as the track outside it, so no per-material setting can keep
// it dry - the difference is spatial, and so is the fix.
const int kMaxShelterVolumes = 8;
uniform int uShelterCount;
uniform mat4 uShelterInverseMatrices[kMaxShelterVolumes];
uniform float uShelterAmounts[kMaxShelterVolumes];
uniform float uShelterFalloffs[kMaxShelterVolumes];

// 1 = fully exposed, 0 = fully sheltered. Volumes overlap by taking the driest
// result, so a pit box inside a garage does not end up wetter than the garage.
float ShelterFactor(vec3 worldPosition) {
    float factor = 1.0;
    for (int i = 0; i < kMaxShelterVolumes; ++i) {
        if (i >= uShelterCount) break;
        vec3 local = (uShelterInverseMatrices[i] * vec4(worldPosition, 1.0)).xyz;
        vec3 insideDistance = vec3(0.5) - abs(local);
        float nearestBoundary = min(insideDistance.x, min(insideDistance.y, insideDistance.z));
        if (nearestBoundary <= 0.0) continue;
        // Fade in from the volume wall so a doorway is a gradient, not a seam.
        float blend = smoothstep(0.0, max(uShelterFalloffs[i], 0.001), nearestBoundary);
        factor = min(factor, mix(1.0, 1.0 - clamp(uShelterAmounts[i], 0.0, 1.0), blend));
    }
    return factor;
}

float WetnessHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float WetnessNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = WetnessHash(cell);
    float b = WetnessHash(cell + vec2(1.0, 0.0));
    float c = WetnessHash(cell + vec2(0.0, 1.0));
    float d = WetnessHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float PuddleMask(vec3 worldPosition, float wetness) {
    if (uWetnessPuddleAmount <= 0.0) return 0.0;
    float scale = max(uWetnessPuddleScale * uMaterialPuddleScale, 0.02);
    vec2 p = worldPosition.xz / scale;
    float n = WetnessNoise(p) * 0.65 + WetnessNoise(p * 2.17 + 13.7) * 0.35;
    float threshold = mix(0.85, 0.25, clamp(wetness * uWetnessPuddleAmount, 0.0, 1.0));
    return smoothstep(threshold, threshold + 0.12, n);
}

vec3 RippleNormal(vec3 worldPosition) {
    if (uWetnessRippleStrength <= 0.0) return vec3(0.0, 1.0, 0.0);
    vec2 p = worldPosition.xz * 3.0;
    float t = uWetnessTime * max(uWetnessRippleSpeed, 0.0);
    float wave1 = sin(p.x * 1.7 + p.y * 0.9 + t * 2.3);
    float wave2 = sin(p.x * -1.1 + p.y * 1.9 + t * 1.7);
    vec2 slope = vec2(wave1, wave2) * uWetnessRippleStrength * 0.06;
    return normalize(vec3(slope.x, 1.0, slope.y));
}

// Beads of water on a surface too steep to pool on.
//
// Mapped in the surface's own tangent plane rather than by world XZ: a world-XZ
// mapping degenerates to infinite streaks on anything vertical, which is exactly
// where droplets are most needed.
vec3 DropletNormal(vec3 worldPosition, vec3 normal, float slope, out float mask) {
    mask = 0.0;
    if (uWetnessDropletStrength <= 0.0) return normal;

    vec3 tangent = abs(normal.y) < 0.95
        ? normalize(cross(normal, vec3(0.0, 1.0, 0.0)))
        : vec3(1.0, 0.0, 0.0);
    vec3 bitangent = cross(normal, tangent);

    float scale = max(uWetnessDropletScale * uMaterialDropletScale, 0.005);
    vec2 uv = vec2(dot(worldPosition, tangent), dot(worldPosition, bitangent)) / scale;

    // Runoff: beads slide down the surface, faster the steeper it is. Without
    // this, droplets on a moving car look painted on.
    vec3 down = vec3(0.0, -1.0, 0.0);
    vec3 downhill = down - normal * dot(down, normal);
    if (dot(downhill, downhill) > 0.0001) {
        downhill = normalize(downhill);
        float drift = uWetnessTime * uWetnessRunoffSpeed * slope;
        uv -= vec2(dot(downhill, tangent), dot(downhill, bitangent)) * drift / scale;
    }

    float n = WetnessNoise(uv);
    mask = smoothstep(0.58, 0.86, n);
    if (mask <= 0.0) return normal;

    // Central differences on the same noise give the bead's surface gradient.
    const float e = 0.5;
    float gx = WetnessNoise(uv + vec2(e, 0.0)) - WetnessNoise(uv - vec2(e, 0.0));
    float gy = WetnessNoise(uv + vec2(0.0, e)) - WetnessNoise(uv - vec2(0.0, e));
    vec3 perturbed = normal - (tangent * gx + bitangent * gy) *
        uWetnessDropletStrength * mask;
    return normalize(perturbed);
}

struct WetSurface {
    vec3 albedo;
    float roughness;
    vec3 normal;
    float puddle;
    float droplet;
    float wetAmount;
    // Additional clear-coat amount/roughness the film contributes. This reuses
    // the material's own clear-coat BRDF lobe (see pbr.fs) instead of faking
    // a wet look purely by tightening the base layer's roughness: a real coat
    // lobe has its own Fresnel term, so it gets the bright grazing-angle sheen
    // that is the single most recognisable "wet" cue, on top of any coat the
    // material already has authored (car clear lacquer, for instance).
    float coatBoost;
    float coatRoughness;
};

WetSurface ApplyWetness(vec3 albedo, float roughness, vec3 normal, vec3 worldPosition) {
    WetSurface result;
    result.albedo = albedo;
    result.roughness = roughness;
    result.normal = normal;
    result.puddle = 0.0;
    result.droplet = 0.0;
    result.wetAmount = 0.0;
    result.coatBoost = 0.0;
    result.coatRoughness = 0.2;

    float response = clamp(uMaterialWetnessResponse, 0.0, 1.0);
    float wetness = clamp(uWetness, 0.0, 1.0) * response * ShelterFactor(worldPosition);
    if (wetness <= 0.0) return result;

    float upFacing = clamp(normal.y, 0.0, 1.0);
    // Everything gets a film, but a vertical face sheds most of it.
    float film = wetness * mix(0.5, 1.0, upFacing);
    // Pooling needs a genuinely level surface - within about 20 degrees of flat,
    // fully committed only inside 6. That is deliberately tight: bodywork (a car
    // roof, a hood) is full of broad, gently domed curves that stay under the old
    // 37-degree cutoff for a large fraction of their area, which read as standing
    // puddles on paint that should only ever bead. Actual ground is normally
    // flat enough that this tightening does not cost it any real puddle coverage.
    float flatness = smoothstep(0.94, 0.995, upFacing);
    float slope = 1.0 - flatness;

    float puddle = PuddleMask(worldPosition, film) * film * flatness *
        clamp(uMaterialPuddleAffinity, 0.0, 1.0);

    float dropletMask = 0.0;
    vec3 dropletNormal = DropletNormal(worldPosition, normal, slope, dropletMask);
    // Beads only exist where water is not already standing.
    float droplet = dropletMask * film * slope * (1.0 - puddle);

    // 1. Darkening. The film does most of it; standing water hides the substrate.
    result.albedo = albedo * mix(1.0, 0.45, film) * mix(1.0, 0.55, puddle);

    // 2. Roughness. Damp tightens, standing water is nearly a mirror, and beads
    //    are smooth where they sit.
    float dampRoughness = mix(roughness, roughness * 0.25, film);
    dampRoughness = mix(dampRoughness, 0.05, droplet);
    result.roughness = clamp(mix(dampRoughness, 0.03, puddle), 0.02, 1.0);

    // 3. Normal. Standing water is level regardless of what is under it; beads
    //    sit on the surface and only perturb it.
    vec3 shaped = mix(normal, dropletNormal, droplet);
    result.normal = normalize(mix(shaped, RippleNormal(worldPosition), puddle));

    // 4. Clear coat. A thin film adds a modest sheen; standing water adds a
    //    strong, near-mirror coat - the same bright rim on a puddle's edge
    //    that is unmistakably "wet" in a way roughness alone cannot sell.
    result.coatBoost = clamp(film * 0.55 + puddle * 0.9, 0.0, 1.0);
    result.coatRoughness = mix(0.22, 0.02, clamp(puddle * 1.4, 0.0, 1.0));

    result.puddle = puddle;
    result.droplet = droplet;
    result.wetAmount = film;
    return result;
}

#endif
