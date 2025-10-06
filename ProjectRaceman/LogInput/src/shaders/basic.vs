#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUV;

uniform mat4 uProj, uView, uModel;
out vec2 vUV;

void main(){
    gl_Position = uProj * uView * uModel * vec4(aPos,1.0);
    vUV = aUV;
}
