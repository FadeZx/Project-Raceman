#version 450 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;
in vec3 vWorldNormal;
void main() {
    vec3 normal = normalize(vWorldNormal);
    NormalBuffer = vec4(normal, 1.0);
    AmbientBuffer = vec4(0.0);
    FragColor = vec4(normal * 0.5 + 0.5, 1.0);
}
