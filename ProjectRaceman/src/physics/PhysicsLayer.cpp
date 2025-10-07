#include "PhysicsLayer.h"

namespace raceman {

void PhysicsLayer::ForEachBody(const BodyVisitor& visitor) const {
    // Placeholder physics integration. In a full implementation this would iterate over
    // simulated rigid bodies and invoke the visitor with up-to-date transforms.
    (void)visitor;
}

} // namespace raceman
