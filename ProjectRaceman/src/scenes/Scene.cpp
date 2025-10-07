#include "Scene.h"

#include "rendering/Renderer.h"
#include "ui/DebugUI.h"

namespace raceman {

Scene::Scene(std::string name, std::shared_ptr<Renderer> renderer)
    : renderer_(std::move(renderer)), name_(std::move(name)) {}

void Scene::OnSceneActivated() {}

void Scene::RenderDebugUi(DebugUI&) {}

} // namespace raceman
