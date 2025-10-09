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

    const std::string& GetName() const { return name_; }

protected:
    std::shared_ptr<Renderer> renderer_;
    std::string name_;
};

} // namespace raceman
