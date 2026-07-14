#include "SceneEditorVehicleBuilder.h"

#include "SceneEditorInternal.h"

#include <algorithm>
#include <utility>

namespace raceman {
namespace {

using namespace scene_editor_internal;

} // namespace

raceman::physics::VehicleConfig BuildDefaultJoltVehicleConfig() {
    raceman::physics::VehicleConfig config;
    config.name = "Default Jolt Vehicle";
    config.chassis.mass = 1200.0f;
    config.chassis.centerOfMassOffset = {0.0f, 0.0f, -0.25f};
    config.engine.idleRPM = 900.0f;
    config.engine.redlineRPM = 6000.0f;
    config.engine.torqueCurve = {{900.0f, 260.0f}, {3500.0f, 420.0f}, {6000.0f, 360.0f}};
    config.transmission.mode = raceman::physics::TransmissionConfig::Mode::Automatic;
    config.transmission.gearRatios = {2.8f, 1.9f, 1.35f, 1.0f, 0.8f};
    config.transmission.finalDriveRatio = 3.42f;
    config.transmission.reverseRatio = -2.9f;
    config.transmission.shiftTime = 0.25f;
    config.frontSuspension.restLength = 0.35f;
    config.rearSuspension.restLength = 0.35f;

    auto makeWheel = [](std::string name, float x, float y, bool front, bool driven) {
        raceman::physics::WheelConfig wheel;
        wheel.name = std::move(name);
        wheel.mountPosition = {x, y, 0.0f};
        wheel.radius = 0.35f;
        wheel.width = 0.22f;
        wheel.inertia = 1.0f;
        wheel.maxSteerAngle = front ? 0.55f : 0.0f;
        wheel.maxBrakingTorque = front ? 3200.0f : 2800.0f;
        wheel.driven = driven;
        wheel.hasBrake = true;
        return wheel;
    };

    config.wheels = {
        makeWheel("Front Left", -0.82f, 1.25f, true, false),
        makeWheel("Front Right", 0.82f, 1.25f, true, false),
        makeWheel("Rear Left", -0.82f, -1.25f, false, true),
        makeWheel("Rear Right", 0.82f, -1.25f, false, true),
    };
    return config;
}

void EnsureDrivableVehicleConfig(raceman::physics::VehicleConfig& config) {
    if (!config.wheels.empty()) {
        return;
    }
    raceman::physics::VehicleConfig fallback = BuildDefaultJoltVehicleConfig();
    fallback.name = config.name.empty() ? fallback.name : config.name;
    fallback.chassis = config.chassis;
    fallback.engine = config.engine;
    fallback.transmission = config.transmission;
    fallback.frontSuspension = config.frontSuspension;
    fallback.rearSuspension = config.rearSuspension;
    fallback.groundContact = config.groundContact;
    fallback.arcadeHandling = config.arcadeHandling;
    fallback.tireGrip = config.tireGrip;
    fallback.tireDynamics = config.tireDynamics;
    fallback.loadTransfer = config.loadTransfer;
    fallback.yawDynamics = config.yawDynamics;
    fallback.brakes = config.brakes;
    fallback.tractionControl = config.tractionControl;
    fallback.differential = config.differential;
    config = std::move(fallback);
}

void RebindRuntimeVehicleWheels(RuntimeVehicleInstance& runtimeVehicle,
                                const SceneObject& vehicleObject,
                                raceman::physics::VehicleConfig& config,
                                const glm::mat4& vehicleWorldMatrix,
                                const VehicleBuilderFindObjectIndex& findObjectIndexById,
                                const VehicleBuilderGetObjectWorldMatrix& getObjectWorldMatrix) {
    runtimeVehicle.wheelObjectIndices.clear();
    runtimeVehicle.wheelBindings.clear();
    runtimeVehicle.wheelAuthoredLocalTransforms.clear();
    runtimeVehicle.wheelAuthoredRotationEuler.clear();

    runtimeVehicle.wheelObjectIndices.reserve(config.wheels.size());
    runtimeVehicle.wheelBindings.reserve(config.wheels.size());
    runtimeVehicle.wheelAuthoredLocalTransforms.reserve(config.wheels.size());
    runtimeVehicle.wheelAuthoredRotationEuler.reserve(config.wheels.size());

    for (std::size_t wheelConfigIndex = 0; wheelConfigIndex < config.wheels.size(); ++wheelConfigIndex) {
        raceman::physics::WheelConfig& wheelConfig = config.wheels[wheelConfigIndex];
        int wheelObjectIndex = -1;
        VehicleWheelBinding runtimeBinding;
        runtimeBinding.wheelName = wheelConfig.name;
        Transform authoredLocalTransform;
        glm::vec3 authoredRotationEuler{0.0f};

        const auto bindingIt = std::find_if(vehicleObject.vehicle.wheelBindings.begin(), vehicleObject.vehicle.wheelBindings.end(),
            [&](const VehicleWheelBinding& binding) {
                return binding.wheelName == wheelConfig.name;
            });
        if (bindingIt != vehicleObject.vehicle.wheelBindings.end()) {
            wheelObjectIndex = findObjectIndexById(bindingIt->objectId);
            runtimeBinding = *bindingIt;
        }

        if (wheelObjectIndex >= 0) {
            const glm::mat4 wheelRelativeMatrix = glm::inverse(vehicleWorldMatrix) * getObjectWorldMatrix(wheelObjectIndex);
            const Transform wheelRelativeTransform = TransformFromMatrix(wheelRelativeMatrix);
            authoredLocalTransform = wheelRelativeTransform;
            authoredRotationEuler = wheelRelativeTransform.rotationEuler;

            const raceman::physics::Vector3 wheelCenterVehicle = SceneVectorToVehicle(wheelRelativeTransform.position);
            const float suspensionRestLength = wheelCenterVehicle.y >= 0.0f
                ? config.frontSuspension.restLength
                : config.rearSuspension.restLength;
            wheelConfig.mountPosition = wheelCenterVehicle + raceman::physics::Vector3{0.0f, 0.0f, suspensionRestLength};
        }

        runtimeVehicle.wheelObjectIndices.push_back(wheelObjectIndex);
        runtimeVehicle.wheelBindings.push_back(std::move(runtimeBinding));
        runtimeVehicle.wheelAuthoredLocalTransforms.push_back(authoredLocalTransform);
        runtimeVehicle.wheelAuthoredRotationEuler.push_back(authoredRotationEuler);
    }
}

} // namespace raceman
