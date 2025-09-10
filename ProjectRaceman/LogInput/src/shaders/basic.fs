#version 330 core
in vec3 vNrm;
in vec3 vPos;
out vec4 FragColor;

uniform vec3 uColor = vec3(0.8,0.8,0.85);
uniform vec3 uLightDir = normalize(vec3(0.4,0.8,0.5));

void main(){
    float NdotL = max(dot(normalize(vNrm), normalize(uLightDir)), 0.1);
    vec3 c = uColor * (0.25 + 0.75 * NdotL);
    FragColor = vec4(c, 1.0);
}
