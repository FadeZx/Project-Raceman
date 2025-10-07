#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace raceman {

struct RigidBodyState {
    glm::mat4 transform{1.0f};
    std::string meshId;
};

class PhysicsLayer {
public:
    using BodyVisitor = std::function<void(const RigidBodyState&)>;

    void ForEachBody(const BodyVisitor& visitor) const;
};

} // namespace raceman
