#pragma once

#include <array>
#include <memory>
#include <string>

#include <glm/glm.hpp>

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

    // Optional: draw if view/projection available
    void Draw(const glm::mat4& view, const glm::mat4& projection);

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
