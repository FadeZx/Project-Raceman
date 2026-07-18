#version 450 core
out vec4 FragColor;
in vec3 vWorldNormal;
void main() {
    FragColor = vec4(normalize(vWorldNormal) * 0.5 + 0.5, 1.0);
}
