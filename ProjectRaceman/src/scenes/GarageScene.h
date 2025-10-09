#pragma once

#include "Scene.h"
#include "../rendering/Skybox.h"
#include "../rendering/shader.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <array>

class Skybox;
class Shader;

struct aiScene;
struct aiMesh;
struct aiNode;

namespace raceman {

class GarageScene : public Scene {
public:
    explicit GarageScene(std::shared_ptr<Renderer> renderer);

    void Init() override;
    void Update(float deltaTime) override;
    void Render(Renderer& renderer) override;
    void RenderDebugUi(DebugUI& ui) override;

private:
    // Per-scene skybox
    std::unique_ptr<Skybox> skybox_;
    std::unique_ptr<Shader> skyboxShader_;
    std::array<std::string, 6> skyboxFaces_{};
    bool skyboxDirty_{true};

    void EnsureSkyboxReady();
    void BuildSkyboxIfNeeded();
    void LoadSkyboxConfig();
    void SaveSkyboxConfig() const;
public:
    // Allow external UI to set faces
    void SetSkyboxFaces(const std::array<std::string,6>& faces);
};

} // namespace raceman
