#pragma once

#include "MathTypes.h"
#include "SimpleJson.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace raceman::physics
{

struct WheelConfig
{
    std::string name{"wheel"};
    Vector3 mountPosition{0.0f, 0.0f, 0.0f};
    float radius{0.35f};
    float width{0.2f};
    float mass{15.0f};
    float inertia{1.0f};
    float maxSteerAngle{0.5f};
    float camber{0.0f};
    float toe{0.0f};
    float gripFactor{1.0f};
    float longitudinalStiffness{10'000.0f};
    float lateralStiffness{8'000.0f};
    float maxBrakingTorque{3'000.0f};
    bool driven{false};
    bool hasBrake{true};
};

struct SuspensionConfig
{
    float restLength{0.35f};
    float springRate{35'000.0f};
    float bumpStopRate{90'000.0f};
    float compressionDamping{2'500.0f};
    float reboundDamping{2'000.0f};
    float antiRollStiffness{5'000.0f};
};

struct DifferentialConfig
{
    float torqueSplit{0.5f};
    float lockingCoefficient{0.15f};
    bool limitedSlip{true};
};

struct TorquePoint
{
    float rpm{0.0f};
    float torque{0.0f};
};

struct EngineConfig
{
    float idleRPM{900.0f};
    float redlineRPM{7'500.0f};
    float stallRPM{600.0f};
    float inertia{0.25f};
    std::vector<TorquePoint> torqueCurve{{1'000.0f, 200.0f}, {6'500.0f, 220.0f}};
};

struct TransmissionConfig
{
    std::vector<float> gearRatios{2.8f, 1.9f, 1.4f, 1.1f, 0.95f, 0.85f};
    float finalDriveRatio{3.42f};
    float reverseRatio{-2.9f};
    float shiftTime{0.25f};
};

struct VehicleChassisConfig
{
    float mass{1'200.0f};
    float yawInertia{2'200.0f};
    float rollInertia{1'100.0f};
    float pitchInertia{1'400.0f};
    Vector3 centerOfMassOffset{0.0f, 0.0f, -0.2f};
};

struct VehicleConfig
{
    std::string name{"default"};
    VehicleChassisConfig chassis{};
    SuspensionConfig frontSuspension{};
    SuspensionConfig rearSuspension{};
    DifferentialConfig differential{};
    EngineConfig engine{};
    TransmissionConfig transmission{};
    std::vector<WheelConfig> wheels{};
};

class VehicleConfigLoader
{
public:
    static VehicleConfig loadFromFile(const std::string &path);
    static std::vector<VehicleConfig> loadDirectory(const std::string &directory, const std::function<bool(const std::string &)> &filter = {});
};

float sampleTorqueCurve(const EngineConfig &config, float rpm);

} // namespace raceman::physics

