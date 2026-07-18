#pragma once

#include <vector>
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>

class Skybox {
public:
    Skybox(const std::vector<std::string>& faces, unsigned int shaderProg);
    ~Skybox();

    void load();
    void draw(glm::mat4 view, glm::mat4 projection);
    unsigned int GetCubemapTexture() const { return cubemapTexture; }

private:
    unsigned int cubemapTexture{0};
    unsigned int skyboxVAO{0}, skyboxVBO{0};
    unsigned int shaderProgram{0};
    std::vector<std::string> faces;

    unsigned int loadCubemap(const std::vector<std::string>& faces); // Updated to match implementation
};
