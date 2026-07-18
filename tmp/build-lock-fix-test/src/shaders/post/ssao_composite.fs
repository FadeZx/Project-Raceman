#version 450 core

layout(location = 0) out vec4 FragColor;
in vec2 vUV;

uniform sampler2D uHdrScene;
uniform sampler2D uAmbientTexture;
uniform sampler2D uSsaoTexture;
uniform sampler2D uNormalTexture;
uniform float uIntensity;

void main() {
    vec4 scene = texture(uHdrScene, vUV);
    vec4 normalSample = texture(uNormalTexture, vUV);
    if (normalSample.a < 0.5) {
        FragColor = scene;
        return;
    }

    vec3 ambient = max(texture(uAmbientTexture, vUV).rgb, vec3(0.0));
    float occlusion = clamp(texture(uSsaoTexture, vUV).r, 0.0, 1.0);
    // Intensity is an artistic multiplier. Do not clamp it to one here: doing so
    // makes values above 1.0 saturate in strongly occluded areas and leaves most
    // of the slider with no visible response in direct-light-heavy scenes.
    float occlusionStrength = max((1.0 - occlusion) * uIntensity, 0.0);
    FragColor = vec4(max(scene.rgb - ambient * occlusionStrength, vec3(0.0)), scene.a);
}
