 // Scene.cpp
#include "Scene.h"
namespace raceman {
    Scene::Scene(std::string name, std::shared_ptr<Renderer> renderer)
        : name_(std::move(name)), renderer_(std::move(renderer)) {
    }

    void Scene::Init() {}

    void Scene::RenderDebugUi(DebugUI&) {}
}
