#pragma once

#include <vector>
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Renderer.h"

class Skybox {
public:
    Skybox(const std::vector<std::string>& faces, unsigned int shaderProg);
    ~Skybox();

    void load();
    // The sky is drawn outside Renderer, so the resolved fog block and the camera
    // position have to travel with the call for the horizon haze to line up with
    // the fog on the geometry in front of it.
    void draw(glm::mat4 view, glm::mat4 projection,
              const raceman::FogUniforms& fog, const glm::vec3& cameraPosition);
    unsigned int GetCubemapTexture() const { return cubemapTexture; }

private:
    unsigned int cubemapTexture{0};
    unsigned int skyboxVAO{0}, skyboxVBO{0};
    unsigned int shaderProgram{0};
    std::vector<std::string> faces;

    unsigned int loadCubemap(const std::vector<std::string>& faces); // Updated to match implementation
};
