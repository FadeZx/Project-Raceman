#pragma once

#include "Scene.h"

#include "../physics/PhysicsLayer.h"
#include <array>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Skybox;
class Shader;

namespace raceman {

class SimulationScene : public Scene {
public:
    explicit SimulationScene(std::shared_ptr<Renderer> renderer);

    void SetPhysicsLayer(std::shared_ptr<PhysicsLayer> physics);

    void Init() override;
    void Update(float deltaTime) override;
    void Render(Renderer& renderer) override;
	void Clean() override;
    void RenderDebugUi(DebugUI& ui) override;
    void Save() override;

public:
    void SetSkyboxFaces(const std::array<std::string,6>& faces);

private:
    std::shared_ptr<PhysicsLayer> physics_;
    // Per-scene skybox
    std::unique_ptr<Skybox> skybox_;
    std::unique_ptr<Shader> skyboxShader_;
    std::array<std::string, 6> skyboxFaces_{};

    void EnsureSkyboxReady();
    void LoadSkyboxConfig();
    void SaveSkyboxConfig() const;
  
};

} // namespace raceman
