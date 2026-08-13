// Skybox.cpp
#include "Skybox.h"
#include <stb_image.h>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

Skybox::Skybox(const std::vector<std::string>& faces, unsigned int shaderProg)
    : faces(faces), shaderProgram(shaderProg) {
    load();
}

Skybox::~Skybox() {
    if (cubemapTexture != 0) glDeleteTextures(1, &cubemapTexture);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
}
void Skybox::load() {
    float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    cubemapTexture = loadCubemap(faces);

}

unsigned int Skybox::loadCubemap(const std::vector<std::string>& faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        if (stbi_is_hdr(faces[i].c_str())) {
            float* data = stbi_loadf(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data) {
                const GLenum format = nrChannels == 4 ? GL_RGBA : GL_RGB;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                             nrChannels == 4 ? GL_RGBA16F : GL_RGB16F,
                             width, height, 0, format, GL_FLOAT, data);
                stbi_image_free(data);
            } else {
                std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            }
        } else {
            unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data) {
                const GLenum format = nrChannels == 4 ? GL_RGBA : GL_RGB;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0,
                             nrChannels == 4 ? GL_SRGB8_ALPHA8 : GL_SRGB8,
                             width, height, 0, format, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            } else {
                std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            }
        }
    }
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

void Skybox::draw(glm::mat4 view, glm::mat4 projection,
                  const raceman::FogUniforms& fog, const glm::vec3& cameraPosition) {
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousDepthFunc = GL_LESS;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(glm::mat4(glm::mat3(view))));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    // Uniforms absent from the program resolve to location -1, which the GL spec
    // defines as a silent no-op, so this stays safe against a hand-edited sky
    // shader that has dropped the fog include.
    glUniform3fv(glGetUniformLocation(shaderProgram, "uCameraPosition"), 1, glm::value_ptr(cameraPosition));
    glUniform1i(glGetUniformLocation(shaderProgram, "uFogAffectsSky"), fog.affectsSky ? 1 : 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "uFogMode"), fog.mode);
    glUniform3fv(glGetUniformLocation(shaderProgram, "uFogColor"), 1, glm::value_ptr(fog.color));
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogDensity"), fog.density);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogHeightFalloff"), fog.heightFalloff);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogBaseHeight"), fog.baseHeight);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogStartDistance"), fog.startDistance);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogMaxOpacity"), fog.maxOpacity);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogLinearStart"), fog.linearStart);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogLinearEnd"), fog.linearEnd);
    glUniform3fv(glGetUniformLocation(shaderProgram, "uFogSunDirection"), 1, glm::value_ptr(fog.sunDirection));
    glUniform3fv(glGetUniformLocation(shaderProgram, "uFogSunColor"), 1, glm::value_ptr(fog.sunColor));
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogSunIntensity"), fog.sunIntensity);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogSunExponent"), fog.sunExponent);
    glUniform1i(glGetUniformLocation(shaderProgram, "uFogDebugView"), fog.debugView ? 1 : 0);
    // The sky shader keeps its own cubemap on unit 0, so the fog sky map goes to
    // unit 1 here. The uniform value is per-program, so this does not have to
    // agree with the unit the material shaders use.
    const bool useSkyColor = fog.useSkyColor && fog.skyMap != 0;
    glUniform1i(glGetUniformLocation(shaderProgram, "uFogUseSkyColor"), useSkyColor ? 1 : 0);
    glUniform1f(glGetUniformLocation(shaderProgram, "uFogSkyMipLevel"), fog.skyMipLevel);
    glUniform1i(glGetUniformLocation(shaderProgram, "uFogSkyMap"), 1);
    if (useSkyColor) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, fog.skyMap);
    }

    glBindVertexArray(skyboxVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthMask(previousDepthMask);
    glDepthFunc(previousDepthFunc);
}
