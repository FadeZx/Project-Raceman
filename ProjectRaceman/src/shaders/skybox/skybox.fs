#version 330 core

#include <common/fog.glsl>

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
layout(location = 3) out vec4 MaterialBuffer;

in vec3 TexCoords;

uniform samplerCube skybox;
uniform vec3 uCameraPosition;
uniform bool uFogAffectsSky;

void main()
{
    NormalBuffer = vec4(0.0);
    AmbientBuffer = vec4(0.0);
    MaterialBuffer = vec4(0.0, 1.0, 0.0, 0.0);
    vec3 color = texture(skybox, TexCoords).rgb;

    // Exponential height fog only. Linear fog is a start/end depth ramp, and the
    // sky sits past any end distance the user could pick, so applying it would
    // always replace the entire skybox with flat fog colour.
    if (uFogAffectsSky && uFogMode == 2) {
        // The sky has no world position, so synthesise one very far along the
        // view ray. Toward the horizon delta.y approaches zero and the height
        // integral saturates into a dense haze band; toward the zenith delta.y
        // is large and the integral collapses, leaving the sky clear. Both fall
        // out of the same formula used for geometry, so the horizon matches the
        // distant objects sitting in front of it.
        vec3 skyDirection = normalize(TexCoords);
        vec3 syntheticWorldPosition = uCameraPosition + skyDirection * 100000.0;
        float transmittance = FogTransmittance(uCameraPosition, syntheticWorldPosition);
        color = color * transmittance + FogInscatter(-skyDirection) * (1.0 - transmittance);
        if (uFogDebugView) color = vec3(transmittance);
    }

    FragColor = vec4(color, 1.0);
}
