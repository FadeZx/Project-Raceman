#pragma once

#include <memory>
#include <string>

namespace raceman {

class Renderer;
class DebugUI;

class Scene : public std::enable_shared_from_this<Scene> {
public:
    explicit Scene(std::string name, std::shared_ptr<Renderer> renderer);
    virtual ~Scene() = default;

    virtual void Init();
    virtual void Update(float deltaTime) = 0;
    virtual void Render(Renderer& renderer) = 0;
    virtual void Clean() {}
    virtual void RenderDebugUi(DebugUI& ui);

    // Save the scene to disk or project state
    virtual void Save() {}

    // Dirty state management
    bool IsDirty() const { return dirty_; }
    void MarkDirty() { dirty_ = true; }
    void MarkClean() { dirty_ = false; }

    const std::string& GetName() const { return name_; }

protected:
    std::shared_ptr<Renderer> renderer_;
    std::string name_;
    bool dirty_{false};
};

} // namespace raceman
