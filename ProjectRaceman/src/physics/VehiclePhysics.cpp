#include "VehiclePhysics.h"

#include <algorithm>
#include <cmath>

namespace raceman::physics
{
namespace
{
constexpr float kGravity = 9.81f;
constexpr float kEpsilon = 1e-5f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

float clamp(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}

float sign(float value)
{
    if (value > 0.0f)
    {
        return 1.0f;
    }
    if (value < 0.0f)
    {
        return -1.0f;
    }
    return 0.0f;
}

} // namespace

VehiclePhysics::VehiclePhysics(const VehicleConfig &config)
    : m_config(config)
{
    m_wheels.reserve(config.wheels.size());
    for (const auto &wheelConfig : config.wheels)
    {
        WheelState state{};
        state.config = wheelConfig;
        state.suspension = (wheelConfig.mountPosition.y >= 0.0f) ? &m_config.frontSuspension : &m_config.rearSuspension;
        state.transform.rotation = Quaternion::identity();
        state.transform.position = Vector3{};
        m_wheels.push_back(state);
    }
    m_wheelTransforms.resize(m_wheels.size());
    m_body.transform.rotation = Quaternion::identity();
    m_body.transform.position = {0.0f, 0.0f, m_config.frontSuspension.restLength + m_config.rearSuspension.restLength};
    m_engineAngularVelocity = m_config.engine.idleRPM * (kTwoPi / 60.0f);
    m_prevEngineRPM = m_config.engine.idleRPM;
}

void VehiclePhysics::setInput(const VehicleControlInput &input)
{
    m_input.throttle = clamp(input.throttle, 0.0f, 1.0f);
    m_input.brake = clamp(input.brake, 0.0f, 1.0f);
    m_input.clutch = clamp(input.clutch, 0.0f, 1.0f);
    m_input.steering = clamp(input.steering, -1.0f, 1.0f);
    m_input.handbrake = clamp(input.handbrake, 0.0f, 1.0f);
}

void VehiclePhysics::setTelemetryCallback(std::function<void(const VehicleTelemetry &)> callback)
{
    m_telemetryCallback = std::move(callback);
}

void VehiclePhysics::shiftUp()
{
    if (m_isNeutral)
    {
        m_isNeutral = false;
        m_currentGear = 0;
        return;
    }
    if (!m_config.transmission.gearRatios.empty())
    {
        m_currentGear = std::min(static_cast<int>(m_config.transmission.gearRatios.size()) - 1, m_currentGear + 1);
    }
}

void VehiclePhysics::shiftDown()
{
    if (m_currentGear > 0)
    {
        --m_currentGear;
    }
    else
    {
        m_isNeutral = true;
    }
}

void VehiclePhysics::setNeutral(bool neutral)
{
    m_isNeutral = neutral;
}

void VehiclePhysics::setReverse(bool reverse)
{
    m_isReverse = reverse;
    if (reverse)
    {
        m_isNeutral = false;
    }
}

const Transform &VehiclePhysics::getChassisTransform() const
{
    return m_body.transform;
}

const std::vector<Transform> &VehiclePhysics::getWheelTransforms() const
{
    return m_wheelTransforms;
}

const VehicleTelemetry &VehiclePhysics::getTelemetry() const
{
    return m_lastTelemetry;
}

void VehiclePhysics::update(float dt)
{
    if (dt < kEpsilon)
    {
        return;
    }

    Vector3 previousVelocity = m_body.linearVelocity;

    float engineRPM = m_engineAngularVelocity * (60.0f / kTwoPi);
    engineRPM = std::max(engineRPM, m_config.engine.stallRPM);
    m_prevEngineRPM = engineRPM;
    float baseEngineTorque = sampleTorqueCurve(m_config.engine, engineRPM);
    float throttleTorque = baseEngineTorque * m_input.throttle;
    float engineBrake = (1.0f - m_input.throttle) * 0.1f * engineRPM;

    float clutchFactor = clamp(1.0f - m_input.clutch, 0.0f, 1.0f);

    float driveRatio = 0.0f;
    if (!m_isNeutral)
    {
        if (m_isReverse)
        {
            driveRatio = m_config.transmission.reverseRatio * m_config.transmission.finalDriveRatio;
        }
        else if (!m_config.transmission.gearRatios.empty())
        {
            int gearIndex = std::clamp(m_currentGear, 0, static_cast<int>(m_config.transmission.gearRatios.size()) - 1);
            driveRatio = m_config.transmission.gearRatios[gearIndex] * m_config.transmission.finalDriveRatio;
        }
    }

    int drivenWheelCount = 0;
    for (const auto &wheel : m_wheels)
    {
        if (wheel.config.driven)
        {
            ++drivenWheelCount;
        }
    }

    float totalDriveTorque = 0.0f;
    if (std::abs(driveRatio) > kEpsilon && drivenWheelCount > 0)
    {
        totalDriveTorque = throttleTorque * driveRatio * clutchFactor;
    }

    float perWheelDriveTorque = (drivenWheelCount > 0) ? (totalDriveTorque / drivenWheelCount) : 0.0f;

    Vector3 totalForce{0.0f, 0.0f, 0.0f};
    Vector3 totalTorque{0.0f, 0.0f, 0.0f};

    float accumulatedWheelSpeed = 0.0f;
    float totalDriveTorqueApplied = 0.0f;

    m_lastTelemetry.wheels.clear();
    m_lastTelemetry.wheels.resize(m_wheels.size());
    m_wheelTransforms.resize(m_wheels.size());

    for (std::size_t i = 0; i < m_wheels.size(); ++i)
    {
        auto &wheel = m_wheels[i];
        const SuspensionConfig &suspension = *wheel.suspension;

        bool isFront = wheel.config.mountPosition.y >= 0.0f;
        float targetSteer = isFront ? (m_input.steering * wheel.config.maxSteerAngle) : 0.0f;
        wheel.steerAngle += (targetSteer - wheel.steerAngle) * clamp(dt * 8.0f, 0.0f, 1.0f);

        Vector3 mountWorld = m_body.transform.position + m_body.transform.rotation.rotate(wheel.config.mountPosition);
        float unsprungLength = mountWorld.z - wheel.config.radius;
        float suspensionLength = clamp(unsprungLength, 0.0f, suspension.restLength);
        float previousCompression = wheel.compression;
        wheel.compression = clamp(suspension.restLength - suspensionLength, 0.0f, suspension.restLength);
        wheel.compressionVelocity = (wheel.compression - previousCompression) / dt;

        float springForce = wheel.compression * suspension.springRate;
        float damping = wheel.compressionVelocity >= 0.0f ? wheel.compressionVelocity * suspension.compressionDamping : wheel.compressionVelocity * suspension.reboundDamping;
        float bumpStop = std::max(0.0f, wheel.compression - suspension.restLength) * suspension.bumpStopRate;
        wheel.normalForce = std::max(0.0f, springForce + damping + bumpStop);

        float suspensionLengthCurrent = suspension.restLength - wheel.compression;
        Vector3 localWheelPos = wheel.config.mountPosition + Vector3{0.0f, 0.0f, -suspensionLengthCurrent};
        Vector3 wheelWorldPos = m_body.transform.position + m_body.transform.rotation.rotate(localWheelPos);

        Quaternion steerQuat = Quaternion::fromAxisAngle({0.0f, 0.0f, 1.0f}, wheel.steerAngle);
        Quaternion wheelOrientation = (m_body.transform.rotation * steerQuat).normalized();
        Quaternion spinQuat = Quaternion::fromAxisAngle({1.0f, 0.0f, 0.0f}, wheel.rotationAngle);
        wheel.transform.position = wheelWorldPos;
        wheel.transform.rotation = (wheelOrientation * spinQuat).normalized();
        m_wheelTransforms[i] = wheel.transform;

        Vector3 forward = wheelOrientation.rotate({0.0f, 1.0f, 0.0f});
        Vector3 right = wheelOrientation.rotate({1.0f, 0.0f, 0.0f});
        Vector3 up = wheelOrientation.rotate({0.0f, 0.0f, 1.0f});

        Vector3 relPos = wheelWorldPos - m_body.transform.position;
        Vector3 wheelVelocity = m_body.linearVelocity + cross(m_body.angularVelocity, relPos);
        float longitudinalSpeed = dot(wheelVelocity, forward);
        float lateralSpeed = dot(wheelVelocity, right);

        float wheelCircumferenceSpeed = wheel.angularVelocity * wheel.config.radius;
        float slipRatio = 0.0f;
        if (std::fabs(longitudinalSpeed) > 0.5f)
        {
            slipRatio = (wheelCircumferenceSpeed - longitudinalSpeed) / std::fabs(longitudinalSpeed);
        }
        else
        {
            slipRatio = wheelCircumferenceSpeed - longitudinalSpeed;
        }

        float maxGripForce = wheel.normalForce * wheel.config.gripFactor;
        float longitudinalForce = clamp(slipRatio * wheel.config.longitudinalStiffness, -maxGripForce, maxGripForce);
        float slipAngle = std::atan2(lateralSpeed, std::fabs(longitudinalSpeed) + 0.1f);
        float lateralForce = clamp(-slipAngle * wheel.config.lateralStiffness, -maxGripForce, maxGripForce);

        wheel.contactForce = forward * longitudinalForce + right * lateralForce + up * wheel.normalForce;
        totalForce += wheel.contactForce;
        totalTorque += cross(relPos, wheel.contactForce);

        float brakeInput = wheel.config.hasBrake ? m_input.brake : 0.0f;
        float handbrakeInput = (!isFront) ? m_input.handbrake : 0.0f;
        wheel.brakeTorque = (brakeInput + handbrakeInput) * wheel.config.maxBrakingTorque;

        wheel.driveTorque = wheel.config.driven ? perWheelDriveTorque : 0.0f;
        if (wheel.config.driven)
        {
            totalDriveTorqueApplied += wheel.driveTorque;
            accumulatedWheelSpeed += wheel.angularVelocity;
        }

        float resistanceTorque = longitudinalForce * wheel.config.radius;
        float rollingResistance = 0.015f * wheel.normalForce * sign(wheel.angularVelocity);
        float brakeTorque = wheel.brakeTorque * sign(wheel.angularVelocity);

        float netWheelTorque = wheel.driveTorque - resistanceTorque - brakeTorque - rollingResistance;
        float angularAcceleration = netWheelTorque / std::max(0.1f, wheel.config.inertia);
        wheel.angularVelocity += angularAcceleration * dt;
        wheel.rotationAngle += wheel.angularVelocity * dt;

        WheelTelemetry telemetry{};
        telemetry.angularVelocity = wheel.angularVelocity;
        telemetry.driveTorque = wheel.driveTorque;
        telemetry.brakeTorque = wheel.brakeTorque;
        telemetry.normalForce = wheel.normalForce;
        telemetry.suspensionTravel = wheel.compression;
        telemetry.force = wheel.contactForce;
        m_lastTelemetry.wheels[i] = telemetry;
    }

    totalForce += Vector3{0.0f, 0.0f, -m_config.chassis.mass * kGravity};

    integrateChassis(dt, totalForce, totalTorque);

    float averageWheelSpeed = (drivenWheelCount > 0) ? (accumulatedWheelSpeed / drivenWheelCount) : 0.0f;
    integrateEngine(dt, driveRatio, averageWheelSpeed, totalDriveTorqueApplied, drivenWheelCount, throttleTorque, engineBrake);

    m_lastTelemetry.engineRPM = m_prevEngineRPM;
    m_lastTelemetry.throttle = m_input.throttle;
    m_lastTelemetry.brake = m_input.brake;
    m_lastTelemetry.steering = m_input.steering;
    m_lastTelemetry.linearVelocity = m_body.linearVelocity;
    m_lastTelemetry.linearAcceleration = (m_body.linearVelocity - previousVelocity) / dt;
    Vector3 forward = m_body.transform.rotation.rotate({0.0f, 1.0f, 0.0f});
    Vector3 right = m_body.transform.rotation.rotate({1.0f, 0.0f, 0.0f});
    m_lastTelemetry.longitudinalSpeed = dot(m_body.linearVelocity, forward);
    m_lastTelemetry.lateralSpeed = dot(m_body.linearVelocity, right);

    if (m_telemetryCallback)
    {
        m_telemetryCallback(m_lastTelemetry);
    }
}

void VehiclePhysics::integrateEngine(float dt, float driveRatio, float averageWheelSpeed, float totalDriveTorqueApplied, int drivenWheels, float throttleTorque, float engineBrakeTorque)
{
    float reactionTorque = (std::abs(driveRatio) > kEpsilon) ? (totalDriveTorqueApplied / driveRatio) : 0.0f;
    float engineDrag = 0.02f * m_engineAngularVelocity;
    float netTorque = throttleTorque - reactionTorque - engineDrag - engineBrakeTorque;

    if (drivenWheels > 0 && std::abs(driveRatio) > kEpsilon)
    {
        float targetAngularVelocity = std::abs(driveRatio) * averageWheelSpeed;
        float slipCorrection = (targetAngularVelocity - m_engineAngularVelocity) * (1.0f - m_input.clutch) * 5.0f;
        netTorque += slipCorrection * m_config.engine.inertia;
    }

    float angularAcceleration = netTorque / std::max(kEpsilon, m_config.engine.inertia);
    m_engineAngularVelocity = std::max(0.0f, m_engineAngularVelocity + angularAcceleration * dt);

    float rpm = m_engineAngularVelocity * (60.0f / kTwoPi);
    rpm = clamp(rpm, m_config.engine.stallRPM, m_config.engine.redlineRPM);
    m_engineAngularVelocity = rpm * (kTwoPi / 60.0f);
    m_prevEngineRPM = rpm;
}

void VehiclePhysics::integrateChassis(float dt, const Vector3 &totalForce, const Vector3 &totalTorque)
{
    Vector3 acceleration = totalForce / std::max(kEpsilon, m_config.chassis.mass);
    m_body.linearVelocity += acceleration * dt;
    m_body.transform.position += m_body.linearVelocity * dt;

    float yawInertia = std::max(kEpsilon, m_config.chassis.yawInertia);
    float yawAcceleration = totalTorque.z / yawInertia;
    m_body.angularVelocity.z += yawAcceleration * dt;

    float yawDelta = m_body.angularVelocity.z * dt;
    Quaternion yawRotation = Quaternion::fromAxisAngle({0.0f, 0.0f, 1.0f}, yawDelta);
    m_body.transform.rotation = (yawRotation * m_body.transform.rotation).normalized();
}

} // namespace raceman::physics

