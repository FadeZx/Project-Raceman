#version 450 core
out vec4 FragColor;
in vec2 vUV;
in vec3 vWorldNormal;
uniform vec4 uColor;
uniform sampler2D uDiffuseTexture;
uniform bool uUseDiffuseTexture;
uniform vec2 uUvTiling;
uniform vec2 uUvOffset;
void main() {
    vec4 graphBase = vec4(1.0, 1.0, 1.0, 1.0);
    vec3 emissive = (vec4(1.0, 1.0, 1.0, 1.0)).rgb;
    float metallic = clamp(0.0, 0.0, 1.0);
    float roughness = clamp(0.5, 0.02, 1.0);
    float alpha = clamp((vec4(1.0, 1.0, 1.0, 1.0)).a, 0.0, 1.0);
    vec3 normalTint = normalize(vWorldNormal) * 0.5 + 0.5;
    vec4 base = graphBase * uColor;
    vec3 lit = base.rgb * mix(vec3(0.9), normalTint, 0.12 + metallic * 0.18) + emissive * (1.0 + (1.0 - roughness));
    FragColor = vec4(lit, base.a * alpha);
}
