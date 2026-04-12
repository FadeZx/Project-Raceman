#pragma once

#include "MathTypes.h"
#include "VehicleConfig.h"

#include <functional>
#include <vector>

namespace raceman::physics
{

struct VehicleControlInput
{
    float throttle{0.0f};
    float brake{0.0f};
    float clutch{0.0f};
    float steering{0.0f};
    float handbrake{0.0f};
};

struct WheelTelemetry
{
    float angularVelocity{0.0f};
    float driveTorque{0.0f};
    float brakeTorque{0.0f};
    float normalForce{0.0f};
    float suspensionTravel{0.0f};
    Vector3 force{};
};

struct VehicleTelemetry
{
    float engineRPM{0.0f};
    float throttle{0.0f};
    float brake{0.0f};
    float steering{0.0f};
    int currentGear{1};
    bool isReverse{false};
    bool isNeutral{false};
    Vector3 linearVelocity{};
    Vector3 linearAcceleration{};
    float longitudinalSpeed{0.0f};
    float lateralSpeed{0.0f};
    std::vector<WheelTelemetry> wheels{};
};

struct VehicleRigidBodyState
{
    Transform transform{};
    Vector3 linearVelocity{};
    Vector3 angularVelocity{};
};

class VehiclePhysics
{
public:
    explicit VehiclePhysics(const VehicleConfig &config);

    void setInput(const VehicleControlInput &input);
    void setTelemetryCallback(std::function<void(const VehicleTelemetry &)> callback);

    void update(float dt);

    const Transform &getChassisTransform() const;
    const std::vector<Transform> &getWheelTransforms() const;
    const VehicleTelemetry &getTelemetry() const;
    void setChassisTransform(const Transform &transform);
    void setRigidBodyState(const VehicleRigidBodyState &state);
    const VehicleRigidBodyState &getRigidBodyState() const;
    const Vector3 &getPendingChassisForce() const;
    const Vector3 &getPendingChassisTorque() const;
    void setExternalBodySimulation(bool external);

    const VehicleConfig &getConfig() const { return m_config; }

    void shiftUp();
    void shiftDown();
    void setNeutral(bool neutral);
    void setReverse(bool reverse);

private:
    struct WheelState
    {
        WheelConfig config{};
        const SuspensionConfig *suspension{nullptr};
        float compression{0.0f};
        float compressionVelocity{0.0f};
        float angularVelocity{0.0f};
        float rotationAngle{0.0f};
        float steerAngle{0.0f};
        float driveTorque{0.0f};
        float brakeTorque{0.0f};
        Vector3 contactForce{};
        float normalForce{0.0f};
        Transform transform{};
    };

    void integrateEngine(float dt, float driveRatio, float averageWheelSpeed, float totalDriveTorqueApplied, int drivenWheels, float throttleTorque, float engineBrakeTorque);
    void integrateChassis(float dt, const Vector3 &totalForce, const Vector3 &totalTorque);

    VehicleConfig m_config;
    VehicleControlInput m_input{};
    VehicleRigidBodyState m_body{};
    std::vector<WheelState> m_wheels;
    std::vector<Transform> m_wheelTransforms;
    VehicleTelemetry m_lastTelemetry{};
    std::function<void(const VehicleTelemetry &)> m_telemetryCallback{};
    Vector3 m_pendingChassisForce{};
    Vector3 m_pendingChassisTorque{};
    bool m_externalBodySimulation{false};

    float m_engineAngularVelocity{0.0f};
    float m_prevEngineRPM{0.0f};

    int m_currentGear{0};
    bool m_isNeutral{false};
    bool m_isReverse{false};
    float m_shiftCooldown{0.0f};
};

} // namespace raceman::physics

