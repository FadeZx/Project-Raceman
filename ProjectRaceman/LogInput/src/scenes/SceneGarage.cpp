// scenes/SceneGarage.cpp
#include "SceneGarage.h"
#include "../core/GlmCompat.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>

#include <iostream>


bool SceneGarage::Init(const SceneContext& ctx) {
    winW_ = ctx.winW; winH_ = ctx.winH;

    shader_ = Render::Shader("../src/shaders/basic.vs", "../src/shaders/basic.fs");

    gridVao_ = Render::SimpleMesh::makeGrid(100, 1.0f);
    axesVao_ = Render::SimpleMesh::makeAxes(1.5f);
    cubeVao_ = Render::SimpleMesh::makeCube();


    cam_.setPerspective(60.0f, float(winW_) / float(winH_), 0.05f, 2000.0f);
    cam_.setTarget({ 0,0,0 });
    cam_.setDistance(8.0f);
    cam_.setLimits(0.8f, 200.0f, -89.0f, 89.0f);
    cam_.updateView();
    return true;
}
void SceneGarage::HandleEvent(const SDL_Event& e) {

    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        cam_.dolly((float)e.wheel.y, 1.0f);
        cam_.updateView();
    }
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = true;
        if (e.button.button == SDL_BUTTON_RIGHT)  rmbDown_ = true;

          std::cout << "[MouseDown] button=" << (int)e.button.button 
                  << " x=" << e.button.x << " y=" << e.button.y << std::endl;
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        lastMX_ = (int)mx;
        lastMY_ = (int)my;

        lastMX_ = mx; lastMY_ = my;
    }
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = false;
        if (e.button.button == SDL_BUTTON_RIGHT)  rmbDown_ = false;
    }
    else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        shiftHeld_ = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    }
    else if (e.type == SDL_EVENT_MOUSE_MOTION) {
        std::cout << "[Motion] x=" << e.motion.x
            << " y=" << e.motion.y
            << " dx=" << e.motion.xrel
            << " dy=" << e.motion.yrel << std::endl;
        handleMouseDrag(e.motion.x, e.motion.y);
    }
}

void SceneGarage::handleMouseDrag(int mx, int my) {

    if (ImGui::GetIO().WantCaptureMouse) return;
    int dx = mx - lastMX_;
    int dy = my - lastMY_;
    lastMX_ = mx; lastMY_ = my;

    if (mmbDown_ && !shiftHeld_) {
        // orbit
        cam_.orbit((float)dx, (float)dy, 0.25f);
        cam_.updateView();
    }
    else if ((mmbDown_ && shiftHeld_) || rmbDown_) {
        // pan
        cam_.pan((float)dx, (float)dy, 0.0025f);
        cam_.updateView();
    }

    glm::vec3 eye = cam_.eye();

}

void SceneGarage::Update(double, double) {


}

void SceneGarage::Render() {
    glViewport(0, 0, winW_, winH_);
    glClearColor(0.08f, 0.08f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader_.use();
    shader_.setMat4("uView", cam_.view());
    shader_.setMat4("uProj", cam_.proj());

    // GRID (grey)
    glm::mat4 M(1.0f);
    shader_.setMat4("uModel", M);
    shader_.setVec3("uColor", 0.30f, 0.32f, 0.36f);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_LINES, 0, Render::SimpleMesh::gridVertexCount(100, 1.0f));

    // AXES at origin (draw each axis with color)
    glBindVertexArray(axesVao_);
    // X
    shader_.setVec3("uColor", 0.9f, 0.2f, 0.2f);
    glDrawArrays(GL_LINES, 0, 2);
    // Y
    shader_.setVec3("uColor", 0.2f, 0.9f, 0.2f);
    glDrawArrays(GL_LINES, 2, 2);
    // Z
    shader_.setVec3("uColor", 0.2f, 0.6f, 0.95f);
    glDrawArrays(GL_LINES, 4, 2);

    // A test cube above the grid so you always see something
    M = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0));
    shader_.setMat4("uModel", M);
    shader_.setVec3("uColor", 0.45f, 0.7f, 0.95f);
    glBindVertexArray(cubeVao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void SceneGarage::ImGuiUI() {
    ImGui::Begin("Garage");
    ImGui::Text("Controls:");
    ImGui::BulletText("MMB drag: orbit");
    ImGui::BulletText("Shift + MMB drag (or RMB drag): pan");
    ImGui::BulletText("Mouse wheel: dolly/zoom");
    ImGui::End();
}
