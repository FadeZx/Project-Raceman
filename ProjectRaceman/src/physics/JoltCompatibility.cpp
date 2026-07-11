#ifdef JPH_DEBUG_RENDERER
#undef JPH_DEBUG_RENDERER
#endif

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ConvexShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

namespace JPH {

void ConvexShape::GetSubmergedVolume(Mat44Arg, Vec3Arg, const Plane&, float& outTotalVolume,
                                     float& outSubmergedVolume, Vec3& outCenterOfBuoyancy) const {
    outTotalVolume = 0.0f;
    outSubmergedVolume = 0.0f;
    outCenterOfBuoyancy = Vec3::sZero();
}

void SphereShape::GetSubmergedVolume(Mat44Arg, Vec3Arg, const Plane&, float& outTotalVolume,
                                     float& outSubmergedVolume, Vec3& outCenterOfBuoyancy) const {
    outTotalVolume = 0.0f;
    outSubmergedVolume = 0.0f;
    outCenterOfBuoyancy = Vec3::sZero();
}

} // namespace JPH
