// scenes/SceneSim.h
#pragma once
#include "IScene.h"
#include <glad/glad.h>
#include "../render/Camera.h"
#include "../render/ChaseCam.h"
#include "../render/Shader.h"
#include "../render/SimpleMesh.h"

class SceneSim : public IScene {
public:
    bool Init(const SceneContext& ctx) override;
    void HandleEvent(const SDL_Event& e) override;
    void Update(double frameDt, double fixedDtAccumulatorStep) override;
    void Render() override;
    void ImGuiUI() override;
    void Shutdown() override;

private:
    Render::Camera        cam_;
    Render::ChaseCamera   chase_;
    Render::ChaseCamParams chaseParams_;
    Render::Shader        shader_;
    GLuint                gridVao_ = 0;
    GLuint                cubeVao_ = 0;

    int winW_ = 1280, winH_ = 720;
    double accumulator_ = 0.0;
    double dtFixed_ = 1.0 / 120.0;

    float kbdThrottle_ = 0.0f;
    float kbdBrake_ = 0.0f;

    float Slew(float current, float target, float risePerS, float fallPerS, float dt);
};
