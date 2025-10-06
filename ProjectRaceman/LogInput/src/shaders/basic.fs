#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform bool useFlatColor;
uniform vec3 uColor;
uniform sampler2D uTex;

void main(){
    FragColor = useFlatColor ? vec4(uColor,1.0) : texture(uTex, vUV);
}
