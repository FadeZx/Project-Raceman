// scenes/SceneGarage.h
#pragma once
#include "IScene.h"
#include <glad/glad.h>
#include "../render/Camera.h"
#include "../render/Shader.h"
#include "../render/SimpleMesh.h"

class SceneGarage : public IScene {
public:
    bool Init(const SceneContext& ctx) override;
    void HandleEvent(const SDL_Event& e) override;
    void Update(double frameDt, double fixedStep) override;
    void Render() override;
    void ImGuiUI() override;
    void Shutdown() override;

private:
    int winW_ = 1280, winH_ = 720;
    Render::Camera cam_;
    Render::Shader shader_;
    GLuint gridVao_ = 0;
    GLuint cubeVao_ = 0;
};
