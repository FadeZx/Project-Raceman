#version 450 core

in vec2 vLocal;
in vec4 vColor;

layout(location = 0) out vec4 FragColor;

// Cheap value noise. A perfectly round puff reads as a ball rather than smoke,
// and a texture lookup is more machinery than breaking up an edge needs.
float Hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float Noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i + vec2(0.0, 0.0)), Hash(i + vec2(1.0, 0.0)), u.x),
               mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

void main() {
    float radius = length(vLocal);
    if (radius > 1.0) discard;

    // Soft radial falloff, squared so the puff has a dense core and a thin
    // edge instead of a linear ramp that reads as a gradient disc.
    float falloff = 1.0 - radius;
    falloff *= falloff;

    // Two octaves of noise, one coarse for the silhouette and one fine for the
    // interior. Kept subtle: heavy noise turns smoke into static.
    float n = Noise(vLocal * 2.5) * 0.65 + Noise(vLocal * 6.0) * 0.35;
    falloff *= mix(0.55, 1.25, n);

    float alpha = clamp(vColor.a * falloff, 0.0, 1.0);
    if (alpha <= 0.002) discard;

    FragColor = vec4(vColor.rgb, alpha);
}
