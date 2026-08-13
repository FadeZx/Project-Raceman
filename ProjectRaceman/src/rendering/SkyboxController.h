#pragma once

#include <array>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Renderer.h"

class Skybox;
class Shader;

namespace raceman {

// Order: +X, -X, +Y, -Y, +Z, -Z
using SkyboxFaces = std::array<std::string, 6>;

class SkyboxController {
public:
    SkyboxController();
    ~SkyboxController();

    // Configure faces; order is posx, negx, posy, negy, posz, negz
    void SetFaces(const SkyboxFaces& faces);
    void SetFace(int index, const std::string& path);
    const SkyboxFaces& GetFaces() const;

    // Shader program used by Skybox
    void SetShaderPaths(const std::string& vsPath, const std::string& fsPath);
    unsigned int GetProgramId() const;
    unsigned int GetCubemapTexture() const;

    // (Re)create underlying Skybox from current faces/shader
    void Reload();

    // Optional: draw if view/projection available. Fog travels with the call so
    // the horizon haze matches the fog applied to geometry; pass a default-built
    // FogUniforms to draw the sky unfogged.
    void Draw(const glm::mat4& view, const glm::mat4& projection,
              const FogUniforms& fog, const glm::vec3& cameraPosition);

private:
    static SkyboxFaces DefaultFaces();

private:
    SkyboxFaces faces_{};
    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Skybox> skybox_;
    std::string vsPath_;
    std::string fsPath_;
};

} // namespace raceman
