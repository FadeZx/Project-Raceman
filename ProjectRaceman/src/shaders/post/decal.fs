#version 450 core

// Screen-space (projected) decals for a forward renderer.
//
// Lighting has already been computed by the time this runs, so a decal cannot
// contribute albedo that gets re-lit. It can only modulate the lit result. That
// is a real limitation, and it is also exactly right for the racing cases this
// exists to serve: skid marks, rubber on the racing line, dirt, oil and grime
// are all fundamentally darkening operations on whatever is underneath.
//
// Reading uDepthTexture / uNormalTexture while they are attached to the target
// framebuffer is only well defined because every fragment reads its OWN texel
// and the renderer issues glTextureBarrier() first. Do not add a neighbourhood
// tap to this shader without moving the pass to its own target.

layout(location = 0) out vec4 FragColor;
// Attachment 2 is the ambient term ssao_composite SUBTRACTS from the scene. A
// decal that darkens colour without darkening this by the same factor makes
// every occluded decal pixel crush to black.
layout(location = 2) out vec4 AmbientBuffer;

uniform sampler2D uDepthTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uDecalTexture;
uniform bool uUseDecalTexture;

uniform mat4 uInverseViewProjection;
uniform mat4 uInverseDecalMatrix;
uniform vec2 uInverseResolution;
uniform vec4 uDecalColor;
uniform float uOpacity;
uniform vec3 uDecalUpWorld;   // decal +Y in world space; decals project along -Y
uniform float uAngleFadeCos;  // below this alignment the decal fades out entirely
uniform vec2 uUvTiling;
uniform vec2 uUvOffset;
uniform int uBlendMode;       // 0 = multiply, 1 = alpha blend

void main() {
    vec2 screenUv = gl_FragCoord.xy * uInverseResolution;
    float depth = texture(uDepthTexture, screenUv).r;
    // Nothing to project onto: the sky is not a surface.
    if (depth >= 1.0) discard;

    vec4 clip = vec4(screenUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    world /= world.w;

    vec3 local = (uInverseDecalMatrix * vec4(world.xyz, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    vec2 decalUv = (local.xz + 0.5) * uUvTiling + uUvOffset;
    vec4 texel = uUseDecalTexture ? texture(uDecalTexture, decalUv) : vec4(1.0);
    vec4 decal = texel * uDecalColor;

    // Angle fade against the surface normal from the forward pass. Without this
    // a decal projected onto a kerb smears down its vertical face, which is the
    // classic giveaway of projected decals.
    float coverage = 1.0;
    vec4 normalSample = texture(uNormalTexture, screenUv);
    if (normalSample.a > 0.5) {
        float alignment = dot(normalize(normalSample.xyz), normalize(uDecalUpWorld));
        coverage *= smoothstep(uAngleFadeCos, min(uAngleFadeCos + 0.25, 1.0), alignment);
    }

    // Soften the volume's side walls so a decal does not end on a hard straight
    // line. The projection axis is deliberately left as a clean clip.
    //
    // Written as 1 - smoothstep(lo, hi, x), NOT smoothstep(hi, lo, x): GLSL
    // leaves smoothstep undefined when edge0 > edge1, and drivers that return 0
    // there make every fragment discard.
    vec2 edgeFade = vec2(1.0) - smoothstep(vec2(0.42), vec2(0.5), abs(local.xz));
    coverage *= edgeFade.x * edgeFade.y;

    float alpha = clamp(decal.a * uOpacity * coverage, 0.0, 1.0);
    if (alpha <= 0.001) discard;

    if (uBlendMode == 0) {
        // Blend func is (GL_DST_COLOR, GL_ZERO): the output IS the multiplier,
        // so an alpha of 0 must resolve to white rather than black.
        vec3 factor = mix(vec3(1.0), decal.rgb, alpha);
        FragColor = vec4(factor, 1.0);
        AmbientBuffer = vec4(factor, 1.0);
    } else {
        // Blend func is (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA). The decal covers
        // the surface, so the ambient underneath is attenuated rather than added
        // to: a projected decal has no ambient contribution of its own.
        FragColor = vec4(decal.rgb, alpha);
        AmbientBuffer = vec4(0.0, 0.0, 0.0, alpha);
    }
}
