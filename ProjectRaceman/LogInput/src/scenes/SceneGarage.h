#pragma once
#include "IScene.h"
#include "../render/Shader.h"
#include "../render/SimpleMesh.h"
#include "../render/camera/OrbitCam.h"

class SceneGarage : public IScene {
public:
    bool Init(const SceneContext& ctx) override;
    void HandleEvent(const SDL_Event& e) override;
    void Update(double frameDt, double fixedDt) override;
    void Render() override;
    void ImGuiUI() override;
    void Shutdown() override {}

private:
    int winW_ = 1280, winH_ = 720;

    Render::Shader shader_;
    GLuint gridVao_ = 0, axesVao_ = 0, cubeVao_ = 0;

    Render::OrbitCamera cam_;
    bool mmbDown_ = false, rmbDown_ = false, shiftHeld_ = false;
    int  lastMX_ = 0, lastMY_ = 0;

    void handleMouseDrag(int mx, int my);
};
