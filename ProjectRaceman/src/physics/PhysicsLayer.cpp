#include "PhysicsLayer.h"

namespace raceman {

void PhysicsLayer::ForEachBody(const BodyVisitor& visitor) const {
    for (const auto& body : bodies_) {
        visitor(body);
    }
}

void PhysicsLayer::SetBodies(std::vector<RigidBodyState> bodies) {
    bodies_ = std::move(bodies);
}

} // namespace raceman
