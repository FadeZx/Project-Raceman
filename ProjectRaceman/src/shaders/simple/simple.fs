#version 450 core
out vec4 FragColor;

uniform vec4 uColor; // RGBA

void main() {
    FragColor = uColor;
}