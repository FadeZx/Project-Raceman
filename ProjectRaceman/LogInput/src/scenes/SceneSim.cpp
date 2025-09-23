// scenes/SceneSim.cpp
#include "SceneSim.h"
#include "../core/GlmCompat.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float SceneSim::Slew(float current, float target, float risePerS, float fallPerS, float dt) {
    float rate = (target > current) ? risePerS : fallPerS;
    float delta = clampf(target - current, -rate * dt, rate * dt);
    return current + delta;
}

bool SceneSim::Init(const SceneContext& ctx) {
    winW_ = ctx.winW; winH_ = ctx.winH; dtFixed_ = ctx.dtFixed;

    shader_ = Render::Shader("src/shaders/basic.vs", "src/shaders/basic.fs");
    gridVao_ = Render::SimpleMesh::makeGrid(200, 1.0f);
    cubeVao_ = Render::SimpleMesh::makeCube();

    cam_.setPerspective(60.0f, float(winW_) / float(winH_), 0.05f, 2000.0f);
    cam_.setView({ -6.f, 4.f, 8.f }, { 0.f, 0.6f, 0.f });
    return true;
}

void SceneSim::HandleEvent(const SDL_Event&) {}

void SceneSim::Update(double frameDt, double) {
    accumulator_ += frameDt;
    // … your fixed-step physics call goes here if needed …
}

void SceneSim::Render() {
    glViewport(0, 0, winW_, winH_);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader_.use();
    shader_.setMat4("uView", cam_.view());
    shader_.setMat4("uProj", cam_.proj());

    glm::mat4 M(1.0f);
    shader_.setMat4("uModel", M);
    shader_.setVec3("uColor", 0.4f, 0.4f, 0.45f);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_LINES, 0, Render::SimpleMesh::gridVertexCount(200, 1.0f));

    shader_.setVec3("uColor", 0.9f, 0.2f, 0.2f);
    glBindVertexArray(cubeVao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void SceneSim::ImGuiUI() {
    ImGui::Begin("Sim");
    ImGui::Text("Sim scene placeholder.");
    ImGui::End();
}

void SceneSim::Shutdown() {}
