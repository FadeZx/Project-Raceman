#version 330 core
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalBuffer;
layout(location = 2) out vec4 AmbientBuffer;

in vec3 TexCoords;

uniform samplerCube skybox;

void main()
{
    NormalBuffer = vec4(0.0);
    AmbientBuffer = vec4(0.0);
    FragColor = texture(skybox, TexCoords);
}
