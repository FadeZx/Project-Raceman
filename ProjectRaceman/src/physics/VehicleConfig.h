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
    bool overrideTire{false};
    bool driven{false};
    bool hasBrake{true};
};

struct VehicleWheelTireConfig
{
    bool enabled{true};
    float gripFactor{1.0f};
    float longitudinalStiffness{10'000.0f};
    float lateralStiffness{8'000.0f};
    float frontGripScale{1.0f};
    float rearGripScale{1.0f};
};

struct ResolvedWheelTireConfig
{
    float gripFactor{1.0f};
    float longitudinalStiffness{10'000.0f};
    float lateralStiffness{8'000.0f};
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

struct VehicleGroundContactConfig
{
    bool enabled{true};
    float probeUp{0.75f};
    float extraProbeLength{0.75f};
    float rideHeightOffset{0.0f};
    float heightSmoothing{18.0f};
    float tiltSmoothing{12.0f};
    float minGroundNormalY{0.2f};
    float obstacleProbeHeight{0.45f};
    float obstacleSkin{0.75f};
    float wallNormalYMax{0.45f};
    float airborneGravity{18.0f};
};

struct VehicleTireGripConfig
{
    bool enabled{true};
    float lateralGrip{5.0f};
    float longitudinalGrip{1.0f};
    float slipAngleLimit{12.0f};
    float slideGripLoss{0.60f};
    float recoveryRate{7.5f};
    float handbrakeGripScale{0.35f};
    float downforceGripScale{0.35f};
    float minTractionScale{0.35f};
};

struct VehicleArcadeHandlingConfig
{
    float maxForwardSpeed{55.0f};
    float maxReverseSpeed{12.0f};
    float acceleration{18.0f};
    float reverseAcceleration{10.0f};
    float brakeDeceleration{36.0f};
    float coastDeceleration{2.5f};
    float handbrakeDeceleration{48.0f};
    float fallbackSteerDegreesPerSecond{85.0f};
    float lowSpeedSteerSpeed{1.25f};
    float lowSpeedSteerFloor{0.52f};
    float lowSpeedSteerInputBoost{0.24f};
    float highSpeedSteerCut{0.58f};
    float idleRPM{900.0f};
    float redlineRPM{6000.0f};
};

struct VehicleTireDynamicsConfig
{
    float frontGripBias{1.0f};
    float rearGripBias{1.0f};
    float lateralRelaxationRate{2.2f};
    float gripRecoveryRate{2.0f};
    float slideFriction{1.4f};
    float maxSideSlipSpeedScale{0.45f};
    float liftOffRearGripLoss{0.35f};
    float brakeRearGripLoss{0.45f};
    float throttleRearGripLoss{0.25f};
    float handbrakeRearGripLoss{0.75f};
    float overSpeedGripLoss{0.35f};
    float yawFromRearSlip{70.0f};
    float yawFromFrontSlip{35.0f};
    float yawInertiaScale{1.0f};
    float yawDrag{1.8f};
    float counterSteerTorque{0.65f};
    float tireScrub{2.0f};
    float velocityAlignmentRate{1.2f};
    float rearSlipYawTorque{1.0f};
    float frontSlipYawDamping{0.8f};
    float brakeYawInstability{0.45f};
    float loadMemory{0.6f};
};

struct VehicleLoadTransferConfig
{
    bool enabled{true};
    float brakePitchAmount{2.0f};
    float throttleSquatAmount{1.2f};
    float lateralRollAmount{3.0f};
    float loadGripEffect{0.35f};
    float aeroDownforce{0.8f};
    float aeroBalance{0.48f};
    float maxAeroGripBoost{0.45f};
    float visualSmoothing{8.0f};
};

struct VehicleYawDynamicsConfig
{
    bool enabled{true};
    float minSpeed{5.0f};
    float steeringYawResponse{65.0f};
    float slipYawResponse{45.0f};
    float maxYawRate{130.0f};
    float yawDamping{5.5f};
    float counterSteerRecovery{1.35f};
    float handbrakeRearSlipBoost{1.6f};
    float throttleRearSlipBoost{0.25f};
    float spinSlipAngle{38.0f};
    float spinYawBoost{1.8f};
    float spinRecovery{4.0f};
    float sideSlipToYaw{0.35f};
};

struct VehicleBrakeAssistConfig
{
    float frontBias{0.62f};
    float maxBrakeForce{1.0f};
    bool absEnabled{true};
    float absSlipLimit{0.55f};
    float absReleaseRate{9.0f};
    float absRecoverRate{5.0f};
};

struct VehicleTractionControlConfig
{
    bool enabled{true};
    float slipLimit{0.45f};
    float cutStrength{0.55f};
    float recoveryRate{4.0f};
    float minThrottleScale{0.35f};
};

struct DifferentialConfig
{
    enum class Type
    {
        Open,
        LimitedSlip,
        Locked
    };

    Type type{Type::LimitedSlip};
    float torqueSplit{0.5f};
    float lockingCoefficient{0.15f};
    bool limitedSlip{true};
    float lockStrength{0.35f};
    float coastLock{0.12f};
    float powerLock{0.45f};
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
    enum class Mode
    {
        Automatic,
        Manual
    };

    Mode mode{Mode::Automatic};
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

struct VehicleSetupConfig
{
    bool enabled{false};
    std::string tireCompound{"Custom"};
    std::string drivetrainLayout{"Custom"};
    std::string handlingBalance{"Custom"};
    float stabilityAssist{0.35f};
    std::string simulationLevel{"Simcade"};
};

struct VehicleConfig
{
    std::string name{"default"};
    VehicleSetupConfig setup{};
    VehicleChassisConfig chassis{};
    SuspensionConfig frontSuspension{};
    SuspensionConfig rearSuspension{};
    VehicleGroundContactConfig groundContact{};
    VehicleArcadeHandlingConfig arcadeHandling{};
    VehicleTireGripConfig tireGrip{};
    VehicleWheelTireConfig wheelTire{};
    VehicleTireDynamicsConfig tireDynamics{};
    VehicleLoadTransferConfig loadTransfer{};
    VehicleYawDynamicsConfig yawDynamics{};
    VehicleBrakeAssistConfig brakes{};
    VehicleTractionControlConfig tractionControl{};
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
    static bool saveToFile(const std::string &path, const VehicleConfig &config, std::string *outError = nullptr);
};

float sampleTorqueCurve(const EngineConfig &config, float rpm);
ResolvedWheelTireConfig resolveWheelTire(const VehicleConfig &config, const WheelConfig &wheel);
void applyVehicleSetupPresets(VehicleConfig &config);

} // namespace raceman::physics

