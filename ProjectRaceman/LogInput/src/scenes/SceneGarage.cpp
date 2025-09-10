// scenes/SceneGarage.cpp
#include "SceneGarage.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>

bool SceneGarage::Init(const SceneContext& ctx) {
    winW_ = ctx.winW; winH_ = ctx.winH;

    shader_ = Render::Shader("src/shaders/basic.vs", "src/shaders/basic.fs");
    gridVao_ = Render::SimpleMesh::makeGrid(60, 1.0f);
    cubeVao_ = Render::SimpleMesh::makeCube();

    cam_.setPerspective(60.0f, float(winW_) / float(winH_), 0.05f, 2000.0f);
    cam_.setView({ -6.f, 4.f, 8.f }, { 0.f, 0.6f, 0.f });
    return true;
}

void SceneGarage::HandleEvent(const SDL_Event&) {}

void SceneGarage::Update(double, double) {}

void SceneGarage::Render() {
    glViewport(0, 0, winW_, winH_);
    glClearColor(0.08f, 0.08f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader_.use();
    shader_.setMat4("uView", cam_.view());
    shader_.setMat4("uProj", cam_.proj());

    glm::mat4 M(1.0f);
    shader_.setMat4("uModel", M);
    shader_.setVec3("uColor", 0.35f, 0.35f, 0.40f);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_LINES, 0, Render::SimpleMesh::gridVertexCount(60, 1.0f));

    shader_.setVec3("uColor", 0.2f, 0.6f, 0.9f);
    glBindVertexArray(cubeVao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void SceneGarage::ImGuiUI() {
    ImGui::Begin("Garage");
    ImGui::Text("Garage scene placeholder.");
    ImGui::End();
}

void SceneGarage::Shutdown() {}
