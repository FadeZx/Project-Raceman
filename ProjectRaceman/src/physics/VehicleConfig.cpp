#include "VehicleConfig.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace raceman::physics
{
namespace
{
std::string escapeJsonString(const std::string &value)
{
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    result.push_back('"');
    return result;
}

std::string lowercaseCopy(std::string value)
{
    for (char &ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool presetEquals(const std::string &value, const char *name)
{
    return lowercaseCopy(value) == lowercaseCopy(name);
}

Vector3 readVector3(const json::Value &value)
{
    const auto &array = value.as_array();
    if (array.size() != 3)
    {
        throw json::ParseError("Vector3 requires three elements");
    }
    return Vector3{
        static_cast<float>(array[0].as_number()),
        static_cast<float>(array[1].as_number()),
        static_cast<float>(array[2].as_number())};
}

std::vector<TorquePoint> readTorqueCurve(const json::Value &value)
{
    std::vector<TorquePoint> curve;
    for (const auto &entry : value.as_array())
    {
        const auto &obj = entry.as_object();
        TorquePoint point{};
        point.rpm = static_cast<float>(obj.at("rpm").as_number());
        point.torque = static_cast<float>(obj.at("torque").as_number());
        curve.push_back(point);
    }
    return curve;
}

TransmissionConfig::Mode transmissionModeFromString(const std::string& value)
{
    if (value == "manual" || value == "Manual")
    {
        return TransmissionConfig::Mode::Manual;
    }
    return TransmissionConfig::Mode::Automatic;
}

const char* transmissionModeToString(TransmissionConfig::Mode mode)
{
    switch (mode)
    {
    case TransmissionConfig::Mode::Manual: return "Manual";
    case TransmissionConfig::Mode::Automatic:
    default: return "Automatic";
    }
}

DifferentialConfig::Type differentialTypeFromString(const std::string& value)
{
    if (value == "Open" || value == "open")
    {
        return DifferentialConfig::Type::Open;
    }
    if (value == "Locked" || value == "locked")
    {
        return DifferentialConfig::Type::Locked;
    }
    return DifferentialConfig::Type::LimitedSlip;
}

const char* differentialTypeToString(DifferentialConfig::Type type)
{
    switch (type)
    {
    case DifferentialConfig::Type::Open: return "Open";
    case DifferentialConfig::Type::Locked: return "Locked";
    case DifferentialConfig::Type::LimitedSlip:
    default: return "LimitedSlip";
    }
}

VehicleSetupConfig readSetup(const json::Value &value)
{
    VehicleSetupConfig setup{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        setup.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("tireCompound"); it != obj.end())
    {
        setup.tireCompound = it->second.as_string();
    }
    if (auto it = obj.find("drivetrainLayout"); it != obj.end())
    {
        setup.drivetrainLayout = it->second.as_string();
    }
    if (auto it = obj.find("handlingBalance"); it != obj.end())
    {
        setup.handlingBalance = it->second.as_string();
    }
    if (auto it = obj.find("stabilityAssist"); it != obj.end())
    {
        setup.stabilityAssist = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("simulationLevel"); it != obj.end())
    {
        setup.simulationLevel = it->second.as_string();
    }
    return setup;
}

WheelConfig readWheel(const json::Value &value)
{
    WheelConfig wheel{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("name"); it != obj.end())
    {
        wheel.name = it->second.as_string();
    }
    if (auto it = obj.find("mountPosition"); it != obj.end())
    {
        wheel.mountPosition = readVector3(it->second);
    }
    if (auto it = obj.find("radius"); it != obj.end())
    {
        wheel.radius = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("width"); it != obj.end())
    {
        wheel.width = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("mass"); it != obj.end())
    {
        wheel.mass = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("inertia"); it != obj.end())
    {
        wheel.inertia = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxSteerAngle"); it != obj.end())
    {
        wheel.maxSteerAngle = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("camber"); it != obj.end())
    {
        wheel.camber = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("toe"); it != obj.end())
    {
        wheel.toe = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("gripFactor"); it != obj.end())
    {
        wheel.gripFactor = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("longitudinalStiffness"); it != obj.end())
    {
        wheel.longitudinalStiffness = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lateralStiffness"); it != obj.end())
    {
        wheel.lateralStiffness = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxBrakingTorque"); it != obj.end())
    {
        wheel.maxBrakingTorque = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("overrideTire"); it != obj.end())
    {
        wheel.overrideTire = it->second.as_bool();
    }
    if (auto it = obj.find("driven"); it != obj.end())
    {
        wheel.driven = it->second.as_bool();
    }
    if (auto it = obj.find("hasBrake"); it != obj.end())
    {
        wheel.hasBrake = it->second.as_bool();
    }
    return wheel;
}

SuspensionConfig readSuspension(const json::Value &value)
{
    SuspensionConfig suspension{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("restLength"); it != obj.end())
    {
        suspension.restLength = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("springRate"); it != obj.end())
    {
        suspension.springRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("bumpStopRate"); it != obj.end())
    {
        suspension.bumpStopRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("compressionDamping"); it != obj.end())
    {
        suspension.compressionDamping = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("reboundDamping"); it != obj.end())
    {
        suspension.reboundDamping = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("antiRollStiffness"); it != obj.end())
    {
        suspension.antiRollStiffness = static_cast<float>(it->second.as_number());
    }
    return suspension;
}

VehicleGroundContactConfig readGroundContact(const json::Value &value)
{
    VehicleGroundContactConfig groundContact{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        groundContact.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("probeUp"); it != obj.end())
    {
        groundContact.probeUp = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("extraProbeLength"); it != obj.end())
    {
        groundContact.extraProbeLength = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("rideHeightOffset"); it != obj.end())
    {
        groundContact.rideHeightOffset = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("heightSmoothing"); it != obj.end())
    {
        groundContact.heightSmoothing = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("tiltSmoothing"); it != obj.end())
    {
        groundContact.tiltSmoothing = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("minGroundNormalY"); it != obj.end())
    {
        groundContact.minGroundNormalY = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("obstacleProbeHeight"); it != obj.end())
    {
        groundContact.obstacleProbeHeight = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("obstacleSkin"); it != obj.end())
    {
        groundContact.obstacleSkin = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("wallNormalYMax"); it != obj.end())
    {
        groundContact.wallNormalYMax = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("airborneGravity"); it != obj.end())
    {
        groundContact.airborneGravity = static_cast<float>(it->second.as_number());
    }
    return groundContact;
}

VehicleTireGripConfig readTireGrip(const json::Value &value)
{
    VehicleTireGripConfig tireGrip{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        tireGrip.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("lateralGrip"); it != obj.end())
    {
        tireGrip.lateralGrip = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("longitudinalGrip"); it != obj.end())
    {
        tireGrip.longitudinalGrip = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("slipAngleLimit"); it != obj.end())
    {
        tireGrip.slipAngleLimit = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("slideGripLoss"); it != obj.end())
    {
        tireGrip.slideGripLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("recoveryRate"); it != obj.end())
    {
        tireGrip.recoveryRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("handbrakeGripScale"); it != obj.end())
    {
        tireGrip.handbrakeGripScale = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("downforceGripScale"); it != obj.end())
    {
        tireGrip.downforceGripScale = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("minTractionScale"); it != obj.end())
    {
        tireGrip.minTractionScale = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("combinedSlip"); it != obj.end())
    {
        tireGrip.combinedSlip = it->second.as_bool();
    }
    return tireGrip;
}

VehicleWheelTireConfig readWheelTire(const json::Value &value)
{
    VehicleWheelTireConfig wheelTire{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        wheelTire.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("gripFactor"); it != obj.end())
    {
        wheelTire.gripFactor = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("longitudinalStiffness"); it != obj.end())
    {
        wheelTire.longitudinalStiffness = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lateralStiffness"); it != obj.end())
    {
        wheelTire.lateralStiffness = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("frontGripScale"); it != obj.end())
    {
        wheelTire.frontGripScale = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("rearGripScale"); it != obj.end())
    {
        wheelTire.rearGripScale = static_cast<float>(it->second.as_number());
    }
    return wheelTire;
}

VehicleArcadeHandlingConfig readArcadeHandling(const json::Value &value)
{
    VehicleArcadeHandlingConfig arcadeHandling{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("maxForwardSpeed"); it != obj.end())
    {
        arcadeHandling.maxForwardSpeed = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxReverseSpeed"); it != obj.end())
    {
        arcadeHandling.maxReverseSpeed = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("acceleration"); it != obj.end())
    {
        arcadeHandling.acceleration = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("reverseAcceleration"); it != obj.end())
    {
        arcadeHandling.reverseAcceleration = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("brakeDeceleration"); it != obj.end())
    {
        arcadeHandling.brakeDeceleration = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("coastDeceleration"); it != obj.end())
    {
        arcadeHandling.coastDeceleration = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("handbrakeDeceleration"); it != obj.end())
    {
        arcadeHandling.handbrakeDeceleration = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("fallbackSteerDegreesPerSecond"); it != obj.end())
    {
        arcadeHandling.fallbackSteerDegreesPerSecond = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lowSpeedSteerSpeed"); it != obj.end())
    {
        arcadeHandling.lowSpeedSteerSpeed = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lowSpeedSteerFloor"); it != obj.end())
    {
        arcadeHandling.lowSpeedSteerFloor = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lowSpeedSteerInputBoost"); it != obj.end())
    {
        arcadeHandling.lowSpeedSteerInputBoost = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("highSpeedSteerCut"); it != obj.end())
    {
        arcadeHandling.highSpeedSteerCut = static_cast<float>(it->second.as_number());
    }
    // idleRPM / redlineRPM used to be duplicated here. They are migrated into
    // EngineConfig by the caller, which can see both objects.
    if (auto it = obj.find("engineDrivenAcceleration"); it != obj.end())
    {
        arcadeHandling.engineDrivenAcceleration = it->second.as_bool();
    }
    return arcadeHandling;
}

VehicleTireDynamicsConfig readTireDynamics(const json::Value &value)
{
    VehicleTireDynamicsConfig tireDynamics{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("frontGripBias"); it != obj.end())
    {
        tireDynamics.frontGripBias = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("rearGripBias"); it != obj.end())
    {
        tireDynamics.rearGripBias = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lateralRelaxationRate"); it != obj.end())
    {
        tireDynamics.lateralRelaxationRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("gripRecoveryRate"); it != obj.end())
    {
        tireDynamics.gripRecoveryRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("slideFriction"); it != obj.end())
    {
        tireDynamics.slideFriction = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxSideSlipSpeedScale"); it != obj.end())
    {
        tireDynamics.maxSideSlipSpeedScale = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("liftOffRearGripLoss"); it != obj.end())
    {
        tireDynamics.liftOffRearGripLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("brakeRearGripLoss"); it != obj.end())
    {
        tireDynamics.brakeRearGripLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("throttleRearGripLoss"); it != obj.end())
    {
        tireDynamics.throttleRearGripLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("handbrakeRearGripLoss"); it != obj.end())
    {
        tireDynamics.handbrakeRearGripLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("overSpeedGripLoss"); it != obj.end())
    {
        tireDynamics.overSpeedGripLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawFromRearSlip"); it != obj.end())
    {
        tireDynamics.yawFromRearSlip = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawFromFrontSlip"); it != obj.end())
    {
        tireDynamics.yawFromFrontSlip = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawInertiaScale"); it != obj.end())
    {
        tireDynamics.yawInertiaScale = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawDrag"); it != obj.end())
    {
        tireDynamics.yawDrag = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("counterSteerTorque"); it != obj.end())
    {
        tireDynamics.counterSteerTorque = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("tireScrub"); it != obj.end())
    {
        tireDynamics.tireScrub = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("velocityAlignmentRate"); it != obj.end())
    {
        tireDynamics.velocityAlignmentRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("rearSlipYawTorque"); it != obj.end())
    {
        tireDynamics.rearSlipYawTorque = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("frontSlipYawDamping"); it != obj.end())
    {
        tireDynamics.frontSlipYawDamping = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("brakeYawInstability"); it != obj.end())
    {
        tireDynamics.brakeYawInstability = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("loadMemory"); it != obj.end())
    {
        tireDynamics.loadMemory = static_cast<float>(it->second.as_number());
    }
    return tireDynamics;
}

VehicleCollisionConfig readCollision(const json::Value &value)
{
    VehicleCollisionConfig collision{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        collision.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("restitution"); it != obj.end())
    {
        collision.restitution = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("friction"); it != obj.end())
    {
        collision.friction = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawResponse"); it != obj.end())
    {
        collision.yawResponse = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxImpactYawRate"); it != obj.end())
    {
        collision.maxImpactYawRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("minImpactSpeed"); it != obj.end())
    {
        collision.minImpactSpeed = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("obstacleMass"); it != obj.end())
    {
        collision.obstacleMass = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("energyLoss"); it != obj.end())
    {
        collision.energyLoss = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("impulseRetention"); it != obj.end())
    {
        collision.impulseRetention = static_cast<float>(it->second.as_number());
    }
    return collision;
}

VehicleLoadTransferConfig readLoadTransfer(const json::Value &value)
{
    VehicleLoadTransferConfig loadTransfer{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        loadTransfer.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("brakePitchAmount"); it != obj.end())
    {
        loadTransfer.brakePitchAmount = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("throttleSquatAmount"); it != obj.end())
    {
        loadTransfer.throttleSquatAmount = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lateralRollAmount"); it != obj.end())
    {
        loadTransfer.lateralRollAmount = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("loadGripEffect"); it != obj.end())
    {
        loadTransfer.loadGripEffect = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("aeroDownforce"); it != obj.end())
    {
        loadTransfer.aeroDownforce = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("aeroBalance"); it != obj.end())
    {
        loadTransfer.aeroBalance = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxAeroGripBoost"); it != obj.end())
    {
        loadTransfer.maxAeroGripBoost = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("visualSmoothing"); it != obj.end())
    {
        loadTransfer.visualSmoothing = static_cast<float>(it->second.as_number());
    }
    return loadTransfer;
}

VehicleYawDynamicsConfig readYawDynamics(const json::Value &value)
{
    VehicleYawDynamicsConfig yawDynamics{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        yawDynamics.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("minSpeed"); it != obj.end())
    {
        yawDynamics.minSpeed = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("steeringYawResponse"); it != obj.end())
    {
        yawDynamics.steeringYawResponse = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("slipYawResponse"); it != obj.end())
    {
        yawDynamics.slipYawResponse = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxYawRate"); it != obj.end())
    {
        yawDynamics.maxYawRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawDamping"); it != obj.end())
    {
        yawDynamics.yawDamping = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("counterSteerRecovery"); it != obj.end())
    {
        yawDynamics.counterSteerRecovery = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("handbrakeRearSlipBoost"); it != obj.end())
    {
        yawDynamics.handbrakeRearSlipBoost = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("throttleRearSlipBoost"); it != obj.end())
    {
        yawDynamics.throttleRearSlipBoost = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("spinSlipAngle"); it != obj.end())
    {
        yawDynamics.spinSlipAngle = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("spinYawBoost"); it != obj.end())
    {
        yawDynamics.spinYawBoost = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("spinRecovery"); it != obj.end())
    {
        yawDynamics.spinRecovery = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("sideSlipToYaw"); it != obj.end())
    {
        yawDynamics.sideSlipToYaw = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("physicalYaw"); it != obj.end())
    {
        yawDynamics.physicalYaw = it->second.as_bool();
    }
    return yawDynamics;
}

VehicleBrakeAssistConfig readBrakeAssist(const json::Value &value)
{
    VehicleBrakeAssistConfig brakes{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("frontBias"); it != obj.end())
    {
        brakes.frontBias = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("maxBrakeForce"); it != obj.end())
    {
        brakes.maxBrakeForce = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("absEnabled"); it != obj.end())
    {
        brakes.absEnabled = it->second.as_bool();
    }
    if (auto it = obj.find("absSlipLimit"); it != obj.end())
    {
        brakes.absSlipLimit = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("absReleaseRate"); it != obj.end())
    {
        brakes.absReleaseRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("absRecoverRate"); it != obj.end())
    {
        brakes.absRecoverRate = static_cast<float>(it->second.as_number());
    }
    return brakes;
}

VehicleTractionControlConfig readTractionControl(const json::Value &value)
{
    VehicleTractionControlConfig tractionControl{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("enabled"); it != obj.end())
    {
        tractionControl.enabled = it->second.as_bool();
    }
    if (auto it = obj.find("slipLimit"); it != obj.end())
    {
        tractionControl.slipLimit = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("cutStrength"); it != obj.end())
    {
        tractionControl.cutStrength = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("recoveryRate"); it != obj.end())
    {
        tractionControl.recoveryRate = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("minThrottleScale"); it != obj.end())
    {
        tractionControl.minThrottleScale = static_cast<float>(it->second.as_number());
    }
    return tractionControl;
}

DifferentialConfig readDifferential(const json::Value &value)
{
    DifferentialConfig differential{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("type"); it != obj.end())
    {
        differential.type = differentialTypeFromString(it->second.as_string());
    }
    if (auto it = obj.find("torqueSplit"); it != obj.end())
    {
        differential.torqueSplit = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("lockingCoefficient"); it != obj.end())
    {
        differential.lockingCoefficient = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("limitedSlip"); it != obj.end())
    {
        differential.limitedSlip = it->second.as_bool();
        if (obj.find("type") == obj.end())
        {
            differential.type = differential.limitedSlip
                ? DifferentialConfig::Type::LimitedSlip
                : DifferentialConfig::Type::Open;
        }
    }
    if (auto it = obj.find("lockStrength"); it != obj.end())
    {
        differential.lockStrength = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("coastLock"); it != obj.end())
    {
        differential.coastLock = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("powerLock"); it != obj.end())
    {
        differential.powerLock = static_cast<float>(it->second.as_number());
    }
    if (obj.find("lockStrength") == obj.end())
    {
        differential.lockStrength = differential.lockingCoefficient;
    }
    return differential;
}

EngineConfig readEngine(const json::Value &value)
{
    EngineConfig engine{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("idleRPM"); it != obj.end())
    {
        engine.idleRPM = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("redlineRPM"); it != obj.end())
    {
        engine.redlineRPM = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("stallRPM"); it != obj.end())
    {
        engine.stallRPM = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("inertia"); it != obj.end())
    {
        engine.inertia = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("torqueCurve"); it != obj.end())
    {
        engine.torqueCurve = readTorqueCurve(it->second);
    }
    return engine;
}

TransmissionConfig readTransmission(const json::Value &value)
{
    TransmissionConfig transmission{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("mode"); it != obj.end())
    {
        transmission.mode = transmissionModeFromString(it->second.as_string());
    }
    if (auto it = obj.find("gearRatios"); it != obj.end())
    {
        transmission.gearRatios.clear();
        for (const auto &ratio : it->second.as_array())
        {
            transmission.gearRatios.push_back(static_cast<float>(ratio.as_number()));
        }
    }
    if (auto it = obj.find("finalDriveRatio"); it != obj.end())
    {
        transmission.finalDriveRatio = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("reverseRatio"); it != obj.end())
    {
        transmission.reverseRatio = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("shiftTime"); it != obj.end())
    {
        transmission.shiftTime = static_cast<float>(it->second.as_number());
    }
    return transmission;
}

VehicleChassisConfig readChassis(const json::Value &value)
{
    VehicleChassisConfig chassis{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("mass"); it != obj.end())
    {
        chassis.mass = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("yawInertia"); it != obj.end())
    {
        chassis.yawInertia = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("rollInertia"); it != obj.end())
    {
        chassis.rollInertia = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("pitchInertia"); it != obj.end())
    {
        chassis.pitchInertia = static_cast<float>(it->second.as_number());
    }
    if (auto it = obj.find("centerOfMassOffset"); it != obj.end())
    {
        chassis.centerOfMassOffset = readVector3(it->second);
    }
    return chassis;
}

VehicleConfig readVehicle(const json::Value &value)
{
    VehicleConfig config{};
    const auto &obj = value.as_object();
    if (auto it = obj.find("name"); it != obj.end())
    {
        config.name = it->second.as_string();
    }
    if (auto it = obj.find("setup"); it != obj.end())
    {
        config.setup = readSetup(it->second);
    }
    if (auto it = obj.find("chassis"); it != obj.end())
    {
        config.chassis = readChassis(it->second);
    }
    if (auto it = obj.find("frontSuspension"); it != obj.end())
    {
        config.frontSuspension = readSuspension(it->second);
    }
    if (auto it = obj.find("rearSuspension"); it != obj.end())
    {
        config.rearSuspension = readSuspension(it->second);
    }
    if (auto it = obj.find("groundContact"); it != obj.end())
    {
        config.groundContact = readGroundContact(it->second);
    }
    if (auto it = obj.find("tireGrip"); it != obj.end())
    {
        config.tireGrip = readTireGrip(it->second);
    }
    const bool hasWheelTire = obj.find("wheelTire") != obj.end();
    if (hasWheelTire)
    {
        config.wheelTire = readWheelTire(obj.at("wheelTire"));
    }
    if (auto it = obj.find("arcadeHandling"); it != obj.end())
    {
        config.arcadeHandling = readArcadeHandling(it->second);

        // Legacy rev range. Older configs carried idleRPM/redlineRPM here
        // and the runtime read them from here, so dropping them outright
        // would quietly change how an existing car drives. Migrate them
        // into EngineConfig instead. The engine block is parsed after this
        // one, so a file that states its own rev range still wins.
        const auto &arcadeObj = it->second.as_object();
        if (auto legacy = arcadeObj.find("idleRPM"); legacy != arcadeObj.end())
        {
            config.engine.idleRPM = static_cast<float>(legacy->second.as_number());
        }
        if (auto legacy = arcadeObj.find("redlineRPM"); legacy != arcadeObj.end())
        {
            config.engine.redlineRPM = static_cast<float>(legacy->second.as_number());
        }
    }
    if (auto it = obj.find("tireDynamics"); it != obj.end())
    {
        config.tireDynamics = readTireDynamics(it->second);
    }
    if (auto it = obj.find("loadTransfer"); it != obj.end())
    {
        config.loadTransfer = readLoadTransfer(it->second);
    }
    if (auto it = obj.find("collision"); it != obj.end())
    {
        config.collision = readCollision(it->second);
    }
    if (auto it = obj.find("yawDynamics"); it != obj.end())
    {
        config.yawDynamics = readYawDynamics(it->second);
    }
    if (auto it = obj.find("brakes"); it != obj.end())
    {
        config.brakes = readBrakeAssist(it->second);
    }
    if (auto it = obj.find("tractionControl"); it != obj.end())
    {
        config.tractionControl = readTractionControl(it->second);
    }
    if (auto it = obj.find("differential"); it != obj.end())
    {
        config.differential = readDifferential(it->second);
    }
    if (auto it = obj.find("engine"); it != obj.end())
    {
        config.engine = readEngine(it->second);
    }

    // Legacy migration: the rev range used to exist on BOTH arcadeHandling and
    // engine, and in most configs the two disagreed. The runtime read the
    // arcadeHandling pair, so those values win here - migrating to the engine
    // block must not silently change how an existing car drives.
    if (auto it = obj.find("arcadeHandling"); it != obj.end() && it->second.is_object())
    {
        const auto &arcade = it->second.as_object();
        if (auto legacy = arcade.find("idleRPM"); legacy != arcade.end() && legacy->second.is_number())
        {
            config.engine.idleRPM = static_cast<float>(legacy->second.as_number());
        }
        if (auto legacy = arcade.find("redlineRPM"); legacy != arcade.end() && legacy->second.is_number())
        {
            config.engine.redlineRPM = static_cast<float>(legacy->second.as_number());
        }
    }
    if (auto it = obj.find("transmission"); it != obj.end())
    {
        config.transmission = readTransmission(it->second);
    }
    if (auto it = obj.find("wheels"); it != obj.end())
    {
        config.wheels.clear();
        for (const auto &wheelValue : it->second.as_array())
        {
            config.wheels.push_back(readWheel(wheelValue));
        }
    }
    if (!hasWheelTire)
    {
        for (WheelConfig &wheel : config.wheels)
        {
            wheel.overrideTire = true;
        }
    }
    applyVehicleSetupPresets(config);
    return config;
}
} // namespace

VehicleConfig VehicleConfigLoader::loadFromFile(const std::string &path)
{
    std::ifstream stream(path);
    if (!stream)
    {
        throw std::runtime_error("Unable to open vehicle config: " + path);
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    json::Value root = json::parse(buffer.str());
    if (root.is_object())
    {
        return readVehicle(root);
    }
    if (root.is_array())
    {
        if (root.as_array().empty())
        {
            throw std::runtime_error("Vehicle config array is empty: " + path);
        }
        return readVehicle(root.as_array().front());
    }
    throw std::runtime_error("Vehicle config root must be object or array: " + path);
}

std::vector<VehicleConfig> VehicleConfigLoader::loadDirectory(const std::string &directory, const std::function<bool(const std::string &)> &filter)
{
    namespace fs = std::filesystem;
    std::vector<VehicleConfig> configs;
    for (const auto &entry : fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto path = entry.path();
        if (!filter || filter(path.string()))
        {
            configs.push_back(loadFromFile(path.string()));
        }
    }
    return configs;
}

bool VehicleConfigLoader::saveToFile(const std::string &path, const VehicleConfig &config, std::string *outError)
{
    std::ofstream stream(path, std::ios::out | std::ios::trunc);
    if (!stream)
    {
        if (outError != nullptr)
        {
            *outError = "Unable to open vehicle config for writing: " + path;
        }
        return false;
    }

    stream << std::fixed << std::setprecision(3);
    stream << "{\n";
    stream << "  \"name\": " << escapeJsonString(config.name) << ",\n";
    stream << "  \"setup\": {\n";
    stream << "    \"enabled\": " << (config.setup.enabled ? "true" : "false") << ",\n";
    stream << "    \"tireCompound\": " << escapeJsonString(config.setup.tireCompound) << ",\n";
    stream << "    \"drivetrainLayout\": " << escapeJsonString(config.setup.drivetrainLayout) << ",\n";
    stream << "    \"handlingBalance\": " << escapeJsonString(config.setup.handlingBalance) << ",\n";
    stream << "    \"stabilityAssist\": " << config.setup.stabilityAssist << ",\n";
    stream << "    \"simulationLevel\": " << escapeJsonString(config.setup.simulationLevel) << "\n";
    stream << "  },\n";
    stream << "  \"chassis\": {\n";
    stream << "    \"mass\": " << config.chassis.mass << ",\n";
    stream << "    \"yawInertia\": " << config.chassis.yawInertia << ",\n";
    stream << "    \"rollInertia\": " << config.chassis.rollInertia << ",\n";
    stream << "    \"pitchInertia\": " << config.chassis.pitchInertia << ",\n";
    stream << "    \"centerOfMassOffset\": [" << config.chassis.centerOfMassOffset.x << ", " << config.chassis.centerOfMassOffset.y << ", " << config.chassis.centerOfMassOffset.z << "]\n";
    stream << "  },\n";
    stream << "  \"frontSuspension\": {\n";
    stream << "    \"restLength\": " << config.frontSuspension.restLength << ",\n";
    stream << "    \"springRate\": " << config.frontSuspension.springRate << ",\n";
    stream << "    \"bumpStopRate\": " << config.frontSuspension.bumpStopRate << ",\n";
    stream << "    \"compressionDamping\": " << config.frontSuspension.compressionDamping << ",\n";
    stream << "    \"reboundDamping\": " << config.frontSuspension.reboundDamping << ",\n";
    stream << "    \"antiRollStiffness\": " << config.frontSuspension.antiRollStiffness << "\n";
    stream << "  },\n";
    stream << "  \"rearSuspension\": {\n";
    stream << "    \"restLength\": " << config.rearSuspension.restLength << ",\n";
    stream << "    \"springRate\": " << config.rearSuspension.springRate << ",\n";
    stream << "    \"bumpStopRate\": " << config.rearSuspension.bumpStopRate << ",\n";
    stream << "    \"compressionDamping\": " << config.rearSuspension.compressionDamping << ",\n";
    stream << "    \"reboundDamping\": " << config.rearSuspension.reboundDamping << ",\n";
    stream << "    \"antiRollStiffness\": " << config.rearSuspension.antiRollStiffness << "\n";
    stream << "  },\n";
    stream << "  \"groundContact\": {\n";
    stream << "    \"enabled\": " << (config.groundContact.enabled ? "true" : "false") << ",\n";
    stream << "    \"probeUp\": " << config.groundContact.probeUp << ",\n";
    stream << "    \"extraProbeLength\": " << config.groundContact.extraProbeLength << ",\n";
    stream << "    \"rideHeightOffset\": " << config.groundContact.rideHeightOffset << ",\n";
    stream << "    \"heightSmoothing\": " << config.groundContact.heightSmoothing << ",\n";
    stream << "    \"tiltSmoothing\": " << config.groundContact.tiltSmoothing << ",\n";
    stream << "    \"minGroundNormalY\": " << config.groundContact.minGroundNormalY << ",\n";
    stream << "    \"obstacleProbeHeight\": " << config.groundContact.obstacleProbeHeight << ",\n";
    stream << "    \"obstacleSkin\": " << config.groundContact.obstacleSkin << ",\n";
    stream << "    \"wallNormalYMax\": " << config.groundContact.wallNormalYMax << ",\n";
    stream << "    \"airborneGravity\": " << config.groundContact.airborneGravity << "\n";
    stream << "  },\n";
    stream << "  \"tireGrip\": {\n";
    stream << "    \"enabled\": " << (config.tireGrip.enabled ? "true" : "false") << ",\n";
    stream << "    \"lateralGrip\": " << config.tireGrip.lateralGrip << ",\n";
    stream << "    \"longitudinalGrip\": " << config.tireGrip.longitudinalGrip << ",\n";
    stream << "    \"slipAngleLimit\": " << config.tireGrip.slipAngleLimit << ",\n";
    stream << "    \"slideGripLoss\": " << config.tireGrip.slideGripLoss << ",\n";
    stream << "    \"recoveryRate\": " << config.tireGrip.recoveryRate << ",\n";
    stream << "    \"handbrakeGripScale\": " << config.tireGrip.handbrakeGripScale << ",\n";
    stream << "    \"downforceGripScale\": " << config.tireGrip.downforceGripScale << ",\n";
    stream << "    \"minTractionScale\": " << config.tireGrip.minTractionScale << ",\n";
    stream << "    \"combinedSlip\": " << (config.tireGrip.combinedSlip ? "true" : "false") << "\n";
    stream << "  },\n";
    stream << "  \"wheelTire\": {\n";
    stream << "    \"enabled\": " << (config.wheelTire.enabled ? "true" : "false") << ",\n";
    stream << "    \"gripFactor\": " << config.wheelTire.gripFactor << ",\n";
    stream << "    \"longitudinalStiffness\": " << config.wheelTire.longitudinalStiffness << ",\n";
    stream << "    \"lateralStiffness\": " << config.wheelTire.lateralStiffness << ",\n";
    stream << "    \"frontGripScale\": " << config.wheelTire.frontGripScale << ",\n";
    stream << "    \"rearGripScale\": " << config.wheelTire.rearGripScale << "\n";
    stream << "  },\n";
    stream << "  \"arcadeHandling\": {\n";
    stream << "    \"maxForwardSpeed\": " << config.arcadeHandling.maxForwardSpeed << ",\n";
    stream << "    \"maxReverseSpeed\": " << config.arcadeHandling.maxReverseSpeed << ",\n";
    stream << "    \"acceleration\": " << config.arcadeHandling.acceleration << ",\n";
    stream << "    \"reverseAcceleration\": " << config.arcadeHandling.reverseAcceleration << ",\n";
    stream << "    \"brakeDeceleration\": " << config.arcadeHandling.brakeDeceleration << ",\n";
    stream << "    \"coastDeceleration\": " << config.arcadeHandling.coastDeceleration << ",\n";
    stream << "    \"handbrakeDeceleration\": " << config.arcadeHandling.handbrakeDeceleration << ",\n";
    stream << "    \"fallbackSteerDegreesPerSecond\": " << config.arcadeHandling.fallbackSteerDegreesPerSecond << ",\n";
    stream << "    \"lowSpeedSteerSpeed\": " << config.arcadeHandling.lowSpeedSteerSpeed << ",\n";
    stream << "    \"lowSpeedSteerFloor\": " << config.arcadeHandling.lowSpeedSteerFloor << ",\n";
    stream << "    \"lowSpeedSteerInputBoost\": " << config.arcadeHandling.lowSpeedSteerInputBoost << ",\n";
    stream << "    \"highSpeedSteerCut\": " << config.arcadeHandling.highSpeedSteerCut << ",\n";
    // The rev range is written once, in the engine block. Emitting it here as
    // well is what let the two drift apart in the first place.
    stream << "    \"engineDrivenAcceleration\": " << (config.arcadeHandling.engineDrivenAcceleration ? "true" : "false") << "\n";
    stream << "  },\n";
    stream << "  \"tireDynamics\": {\n";
    stream << "    \"frontGripBias\": " << config.tireDynamics.frontGripBias << ",\n";
    stream << "    \"rearGripBias\": " << config.tireDynamics.rearGripBias << ",\n";
    stream << "    \"lateralRelaxationRate\": " << config.tireDynamics.lateralRelaxationRate << ",\n";
    stream << "    \"gripRecoveryRate\": " << config.tireDynamics.gripRecoveryRate << ",\n";
    stream << "    \"slideFriction\": " << config.tireDynamics.slideFriction << ",\n";
    stream << "    \"maxSideSlipSpeedScale\": " << config.tireDynamics.maxSideSlipSpeedScale << ",\n";
    stream << "    \"liftOffRearGripLoss\": " << config.tireDynamics.liftOffRearGripLoss << ",\n";
    stream << "    \"brakeRearGripLoss\": " << config.tireDynamics.brakeRearGripLoss << ",\n";
    stream << "    \"throttleRearGripLoss\": " << config.tireDynamics.throttleRearGripLoss << ",\n";
    stream << "    \"handbrakeRearGripLoss\": " << config.tireDynamics.handbrakeRearGripLoss << ",\n";
    stream << "    \"overSpeedGripLoss\": " << config.tireDynamics.overSpeedGripLoss << ",\n";
    stream << "    \"yawFromRearSlip\": " << config.tireDynamics.yawFromRearSlip << ",\n";
    stream << "    \"yawFromFrontSlip\": " << config.tireDynamics.yawFromFrontSlip << ",\n";
    stream << "    \"yawInertiaScale\": " << config.tireDynamics.yawInertiaScale << ",\n";
    stream << "    \"yawDrag\": " << config.tireDynamics.yawDrag << ",\n";
    stream << "    \"counterSteerTorque\": " << config.tireDynamics.counterSteerTorque << ",\n";
    stream << "    \"tireScrub\": " << config.tireDynamics.tireScrub << ",\n";
    stream << "    \"velocityAlignmentRate\": " << config.tireDynamics.velocityAlignmentRate << ",\n";
    stream << "    \"rearSlipYawTorque\": " << config.tireDynamics.rearSlipYawTorque << ",\n";
    stream << "    \"frontSlipYawDamping\": " << config.tireDynamics.frontSlipYawDamping << ",\n";
    stream << "    \"brakeYawInstability\": " << config.tireDynamics.brakeYawInstability << ",\n";
    stream << "    \"loadMemory\": " << config.tireDynamics.loadMemory << "\n";
    stream << "  },\n";
    stream << "  \"loadTransfer\": {\n";
    stream << "    \"enabled\": " << (config.loadTransfer.enabled ? "true" : "false") << ",\n";
    stream << "    \"brakePitchAmount\": " << config.loadTransfer.brakePitchAmount << ",\n";
    stream << "    \"throttleSquatAmount\": " << config.loadTransfer.throttleSquatAmount << ",\n";
    stream << "    \"lateralRollAmount\": " << config.loadTransfer.lateralRollAmount << ",\n";
    stream << "    \"loadGripEffect\": " << config.loadTransfer.loadGripEffect << ",\n";
    stream << "    \"aeroDownforce\": " << config.loadTransfer.aeroDownforce << ",\n";
    stream << "    \"aeroBalance\": " << config.loadTransfer.aeroBalance << ",\n";
    stream << "    \"maxAeroGripBoost\": " << config.loadTransfer.maxAeroGripBoost << ",\n";
    stream << "    \"visualSmoothing\": " << config.loadTransfer.visualSmoothing << "\n";
    stream << "  },\n";
    stream << "  \"collision\": {\n";
    stream << "    \"enabled\": " << (config.collision.enabled ? "true" : "false") << ",\n";
    stream << "    \"restitution\": " << config.collision.restitution << ",\n";
    stream << "    \"friction\": " << config.collision.friction << ",\n";
    stream << "    \"yawResponse\": " << config.collision.yawResponse << ",\n";
    stream << "    \"maxImpactYawRate\": " << config.collision.maxImpactYawRate << ",\n";
    stream << "    \"minImpactSpeed\": " << config.collision.minImpactSpeed << ",\n";
    stream << "    \"obstacleMass\": " << config.collision.obstacleMass << ",\n";
    stream << "    \"energyLoss\": " << config.collision.energyLoss << ",\n";
    stream << "    \"impulseRetention\": " << config.collision.impulseRetention << "\n";
    stream << "  },\n";
    stream << "  \"yawDynamics\": {\n";
    stream << "    \"enabled\": " << (config.yawDynamics.enabled ? "true" : "false") << ",\n";
    stream << "    \"minSpeed\": " << config.yawDynamics.minSpeed << ",\n";
    stream << "    \"steeringYawResponse\": " << config.yawDynamics.steeringYawResponse << ",\n";
    stream << "    \"slipYawResponse\": " << config.yawDynamics.slipYawResponse << ",\n";
    stream << "    \"maxYawRate\": " << config.yawDynamics.maxYawRate << ",\n";
    stream << "    \"yawDamping\": " << config.yawDynamics.yawDamping << ",\n";
    stream << "    \"counterSteerRecovery\": " << config.yawDynamics.counterSteerRecovery << ",\n";
    stream << "    \"handbrakeRearSlipBoost\": " << config.yawDynamics.handbrakeRearSlipBoost << ",\n";
    stream << "    \"throttleRearSlipBoost\": " << config.yawDynamics.throttleRearSlipBoost << ",\n";
    stream << "    \"spinSlipAngle\": " << config.yawDynamics.spinSlipAngle << ",\n";
    stream << "    \"spinYawBoost\": " << config.yawDynamics.spinYawBoost << ",\n";
    stream << "    \"spinRecovery\": " << config.yawDynamics.spinRecovery << ",\n";
    stream << "    \"sideSlipToYaw\": " << config.yawDynamics.sideSlipToYaw << ",\n";
    stream << "    \"physicalYaw\": " << (config.yawDynamics.physicalYaw ? "true" : "false") << "\n";
    stream << "  },\n";
    stream << "  \"brakes\": {\n";
    stream << "    \"frontBias\": " << config.brakes.frontBias << ",\n";
    stream << "    \"maxBrakeForce\": " << config.brakes.maxBrakeForce << ",\n";
    stream << "    \"absEnabled\": " << (config.brakes.absEnabled ? "true" : "false") << ",\n";
    stream << "    \"absSlipLimit\": " << config.brakes.absSlipLimit << ",\n";
    stream << "    \"absReleaseRate\": " << config.brakes.absReleaseRate << ",\n";
    stream << "    \"absRecoverRate\": " << config.brakes.absRecoverRate << "\n";
    stream << "  },\n";
    stream << "  \"tractionControl\": {\n";
    stream << "    \"enabled\": " << (config.tractionControl.enabled ? "true" : "false") << ",\n";
    stream << "    \"slipLimit\": " << config.tractionControl.slipLimit << ",\n";
    stream << "    \"cutStrength\": " << config.tractionControl.cutStrength << ",\n";
    stream << "    \"recoveryRate\": " << config.tractionControl.recoveryRate << ",\n";
    stream << "    \"minThrottleScale\": " << config.tractionControl.minThrottleScale << "\n";
    stream << "  },\n";
    stream << "  \"differential\": {\n";
    stream << "    \"type\": " << escapeJsonString(differentialTypeToString(config.differential.type)) << ",\n";
    stream << "    \"torqueSplit\": " << config.differential.torqueSplit << ",\n";
    stream << "    \"lockStrength\": " << config.differential.lockStrength << ",\n";
    stream << "    \"coastLock\": " << config.differential.coastLock << ",\n";
    stream << "    \"powerLock\": " << config.differential.powerLock << "\n";
    stream << "  },\n";
    stream << "  \"engine\": {\n";
    stream << "    \"idleRPM\": " << config.engine.idleRPM << ",\n";
    stream << "    \"redlineRPM\": " << config.engine.redlineRPM << ",\n";
    stream << "    \"stallRPM\": " << config.engine.stallRPM << ",\n";
    stream << "    \"inertia\": " << config.engine.inertia << ",\n";
    stream << "    \"torqueCurve\": [\n";
    for (std::size_t i = 0; i < config.engine.torqueCurve.size(); ++i)
    {
        const TorquePoint &point = config.engine.torqueCurve[i];
        stream << "      {\"rpm\": " << point.rpm << ", \"torque\": " << point.torque << "}";
        stream << (i + 1 < config.engine.torqueCurve.size() ? ",\n" : "\n");
    }
    stream << "    ]\n";
    stream << "  },\n";
    stream << "  \"transmission\": {\n";
    stream << "    \"mode\": " << escapeJsonString(transmissionModeToString(config.transmission.mode)) << ",\n";
    stream << "    \"gearRatios\": [";
    for (std::size_t i = 0; i < config.transmission.gearRatios.size(); ++i)
    {
        stream << config.transmission.gearRatios[i];
        stream << (i + 1 < config.transmission.gearRatios.size() ? ", " : "");
    }
    stream << "],\n";
    stream << "    \"finalDriveRatio\": " << config.transmission.finalDriveRatio << ",\n";
    stream << "    \"reverseRatio\": " << config.transmission.reverseRatio << ",\n";
    stream << "    \"shiftTime\": " << config.transmission.shiftTime << "\n";
    stream << "  },\n";
    stream << "  \"wheels\": [\n";
    for (std::size_t i = 0; i < config.wheels.size(); ++i)
    {
        const WheelConfig &wheel = config.wheels[i];
        stream << "    {\n";
        stream << "      \"name\": " << escapeJsonString(wheel.name) << ",\n";
        stream << "      \"mountPosition\": [" << wheel.mountPosition.x << ", " << wheel.mountPosition.y << ", " << wheel.mountPosition.z << "],\n";
        stream << "      \"radius\": " << wheel.radius << ",\n";
        stream << "      \"width\": " << wheel.width << ",\n";
        stream << "      \"mass\": " << wheel.mass << ",\n";
        stream << "      \"inertia\": " << wheel.inertia << ",\n";
        stream << "      \"maxSteerAngle\": " << wheel.maxSteerAngle << ",\n";
        stream << "      \"camber\": " << wheel.camber << ",\n";
        stream << "      \"toe\": " << wheel.toe << ",\n";
        stream << "      \"gripFactor\": " << wheel.gripFactor << ",\n";
        stream << "      \"longitudinalStiffness\": " << wheel.longitudinalStiffness << ",\n";
        stream << "      \"lateralStiffness\": " << wheel.lateralStiffness << ",\n";
        stream << "      \"maxBrakingTorque\": " << wheel.maxBrakingTorque << ",\n";
        stream << "      \"overrideTire\": " << (wheel.overrideTire ? "true" : "false") << ",\n";
        stream << "      \"driven\": " << (wheel.driven ? "true" : "false") << ",\n";
        stream << "      \"hasBrake\": " << (wheel.hasBrake ? "true" : "false") << "\n";
        stream << "    }" << (i + 1 < config.wheels.size() ? ",\n" : "\n");
    }
    stream << "  ]\n";
    stream << "}\n";

    if (!stream.good())
    {
        if (outError != nullptr)
        {
            *outError = "Failed while writing vehicle config: " + path;
        }
        return false;
    }
    return true;
}

void applyVehicleSetupPresets(VehicleConfig &config)
{
    if (!config.setup.enabled)
    {
        return;
    }

    const std::string tireCompound = lowercaseCopy(config.setup.tireCompound);
    if (tireCompound != "custom")
    {
        config.tireGrip.enabled = true;
        config.wheelTire.enabled = true;
        for (WheelConfig &wheel : config.wheels)
        {
            wheel.overrideTire = false;
        }

        if (presetEquals(tireCompound, "SlickSoft"))
        {
            config.wheelTire.gripFactor = 3.55f;
            config.wheelTire.longitudinalStiffness = 18500.0f;
            config.wheelTire.lateralStiffness = 15000.0f;
            config.wheelTire.frontGripScale = 1.03f;
            config.wheelTire.rearGripScale = 1.00f;
            config.tireGrip.lateralGrip = 7.8f;
            config.tireGrip.longitudinalGrip = 0.72f;
            config.tireGrip.slipAngleLimit = 15.0f;
            config.tireGrip.slideGripLoss = 0.42f;
            config.tireGrip.recoveryRate = 8.5f;
            config.tireGrip.minTractionScale = 0.62f;
            config.tireDynamics.slideFriction = 3.7f;
            config.tireDynamics.tireScrub = 4.1f;
        }
        else if (presetEquals(tireCompound, "SlickHard"))
        {
            config.wheelTire.gripFactor = 2.95f;
            config.wheelTire.longitudinalStiffness = 15000.0f;
            config.wheelTire.lateralStiffness = 11200.0f;
            config.wheelTire.frontGripScale = 1.02f;
            config.wheelTire.rearGripScale = 1.00f;
            config.tireGrip.lateralGrip = 6.4f;
            config.tireGrip.longitudinalGrip = 0.62f;
            config.tireGrip.slipAngleLimit = 14.0f;
            config.tireGrip.slideGripLoss = 0.48f;
            config.tireGrip.recoveryRate = 7.0f;
            config.tireGrip.minTractionScale = 0.55f;
            config.tireDynamics.slideFriction = 3.1f;
            config.tireDynamics.tireScrub = 3.4f;
        }
        else if (presetEquals(tireCompound, "Wet"))
        {
            config.wheelTire.gripFactor = 2.35f;
            config.wheelTire.longitudinalStiffness = 12000.0f;
            config.wheelTire.lateralStiffness = 9400.0f;
            config.wheelTire.frontGripScale = 1.04f;
            config.wheelTire.rearGripScale = 0.98f;
            config.tireGrip.lateralGrip = 4.8f;
            config.tireGrip.longitudinalGrip = 0.48f;
            config.tireGrip.slipAngleLimit = 12.5f;
            config.tireGrip.slideGripLoss = 0.58f;
            config.tireGrip.recoveryRate = 5.2f;
            config.tireGrip.minTractionScale = 0.42f;
            config.tireDynamics.slideFriction = 2.4f;
            config.tireDynamics.tireScrub = 2.8f;
        }
        else if (presetEquals(tireCompound, "Street"))
        {
            config.wheelTire.gripFactor = 2.10f;
            config.wheelTire.longitudinalStiffness = 10500.0f;
            config.wheelTire.lateralStiffness = 8500.0f;
            config.wheelTire.frontGripScale = 1.00f;
            config.wheelTire.rearGripScale = 1.00f;
            config.tireGrip.lateralGrip = 4.2f;
            config.tireGrip.longitudinalGrip = 0.55f;
            config.tireGrip.slipAngleLimit = 11.5f;
            config.tireGrip.slideGripLoss = 0.55f;
            config.tireGrip.recoveryRate = 5.8f;
            config.tireGrip.minTractionScale = 0.45f;
            config.tireDynamics.slideFriction = 2.5f;
            config.tireDynamics.tireScrub = 2.7f;
        }
        else if (presetEquals(tireCompound, "Drift"))
        {
            config.wheelTire.gripFactor = 1.65f;
            config.wheelTire.longitudinalStiffness = 9000.0f;
            config.wheelTire.lateralStiffness = 7000.0f;
            config.wheelTire.frontGripScale = 1.08f;
            config.wheelTire.rearGripScale = 0.84f;
            config.tireGrip.lateralGrip = 3.2f;
            config.tireGrip.longitudinalGrip = 0.55f;
            config.tireGrip.slipAngleLimit = 24.0f;
            config.tireGrip.slideGripLoss = 0.34f;
            config.tireGrip.recoveryRate = 3.0f;
            config.tireGrip.minTractionScale = 0.35f;
            config.tireDynamics.slideFriction = 1.9f;
            config.tireDynamics.tireScrub = 2.1f;
        }
        else
        {
            config.wheelTire.gripFactor = 3.25f;
            config.wheelTire.longitudinalStiffness = 16800.0f;
            config.wheelTire.lateralStiffness = 12500.0f;
            config.wheelTire.frontGripScale = 1.04f;
            config.wheelTire.rearGripScale = 1.00f;
            config.tireGrip.lateralGrip = 7.15f;
            config.tireGrip.longitudinalGrip = 0.58f;
            config.tireGrip.slipAngleLimit = 16.0f;
            config.tireGrip.slideGripLoss = 0.48f;
            config.tireGrip.recoveryRate = 8.0f;
            config.tireGrip.minTractionScale = 0.58f;
            config.tireDynamics.slideFriction = 3.25f;
            config.tireDynamics.tireScrub = 3.6f;
        }
    }

    const std::string drivetrainLayout = lowercaseCopy(config.setup.drivetrainLayout);
    if (drivetrainLayout != "custom")
    {
        for (WheelConfig &wheel : config.wheels)
        {
            const bool front = wheel.mountPosition.y >= 0.0f;
            if (presetEquals(drivetrainLayout, "FWD"))
            {
                wheel.driven = front;
            }
            else if (presetEquals(drivetrainLayout, "AWD"))
            {
                wheel.driven = true;
            }
            else
            {
                wheel.driven = !front;
            }
        }

        if (presetEquals(drivetrainLayout, "FWD"))
        {
            config.differential.type = DifferentialConfig::Type::LimitedSlip;
            config.differential.torqueSplit = 1.0f;
        }
        else if (presetEquals(drivetrainLayout, "AWD"))
        {
            config.differential.type = DifferentialConfig::Type::LimitedSlip;
            config.differential.torqueSplit = 0.50f;
        }
        else
        {
            config.differential.type = DifferentialConfig::Type::LimitedSlip;
            config.differential.torqueSplit = 0.0f;
        }
    }

    const std::string handlingBalance = lowercaseCopy(config.setup.handlingBalance);
    if (presetEquals(handlingBalance, "Stable"))
    {
        config.tireDynamics.frontGripBias = 1.00f;
        config.tireDynamics.rearGripBias = 1.13f;
        config.tireDynamics.yawFromRearSlip = 54.0f;
        config.tireDynamics.yawFromFrontSlip = 36.0f;
        config.tireDynamics.yawInertiaScale = 0.82f;
        config.tireDynamics.yawDrag = 3.1f;
        config.yawDynamics.slipYawResponse = 38.0f;
        config.yawDynamics.yawDamping = 6.7f;
        config.yawDynamics.spinYawBoost = 1.1f;
    }
    else if (presetEquals(handlingBalance, "Loose"))
    {
        config.tireDynamics.frontGripBias = 1.12f;
        config.tireDynamics.rearGripBias = 0.94f;
        config.tireDynamics.yawFromRearSlip = 78.0f;
        config.tireDynamics.yawFromFrontSlip = 26.0f;
        config.tireDynamics.yawInertiaScale = 0.68f;
        config.tireDynamics.yawDrag = 2.1f;
        config.yawDynamics.slipYawResponse = 54.0f;
        config.yawDynamics.yawDamping = 4.8f;
        config.yawDynamics.spinYawBoost = 1.6f;
    }
    else if (presetEquals(handlingBalance, "Neutral"))
    {
        config.tireDynamics.frontGripBias = 1.05f;
        config.tireDynamics.rearGripBias = 1.04f;
        config.tireDynamics.yawFromRearSlip = 66.0f;
        config.tireDynamics.yawFromFrontSlip = 30.0f;
        config.tireDynamics.yawInertiaScale = 0.74f;
        config.tireDynamics.yawDrag = 2.55f;
        config.yawDynamics.slipYawResponse = 46.0f;
        config.yawDynamics.yawDamping = 5.6f;
        config.yawDynamics.spinYawBoost = 1.35f;
    }

    const float assist = std::clamp(config.setup.stabilityAssist, 0.0f, 1.0f);
    config.brakes.absEnabled = assist > 0.08f;
    config.tractionControl.enabled = assist > 0.08f;
    config.tractionControl.cutStrength = 0.25f + assist * 0.55f;
    config.tractionControl.minThrottleScale = 0.25f + assist * 0.45f;
    config.tractionControl.recoveryRate = 3.0f + assist * 6.0f;
    config.brakes.absReleaseRate = 5.0f + assist * 7.0f;
    config.brakes.absRecoverRate = 4.0f + assist * 5.0f;
    config.tireDynamics.frontSlipYawDamping = 0.55f + assist * 0.60f;
    config.tireDynamics.counterSteerTorque = 0.42f + assist * 0.52f;
    config.yawDynamics.spinRecovery = 2.8f + assist * 3.6f;

    // ---------------------------------------------------------------------
    // Simulation level. The blocks above build the car; this one decides how
    // much of the driving it does for you.
    //
    // The lateral model weighs a corner demand that cannot exceed 1.0
    // (steering * speedFraction^2) against tireGrip.lateralGrip / 5. Slick
    // grip alone puts that limit near 1.6, out of the driver reach, which is
    // why a GT car on the untouched table sticks however it is thrown at a
    // corner. Pulling lateralGrip down is what brings the limit back into
    // range; the rest shapes what happens once it is crossed.
    //
    // Values the compound and balance tables own were just rebuilt from those
    // tables, so scaling them here is repeatable. On a Custom compound or
    // balance the author owns those numbers, and repeated applies must not
    // walk them down, so the level clamps to an absolute bound instead.
    const bool compoundManaged = tireCompound != "custom";
    const bool balanceManaged = handlingBalance != "custom";
    auto tune = [](float &value, bool managed, float scale, float bound) {
        value = managed
            ? value * scale
            : (scale < 1.0f ? (std::min)(value, bound) : (std::max)(value, bound));
    };

    const std::string simulationLevel = lowercaseCopy(config.setup.simulationLevel);
    if (presetEquals(simulationLevel, "Arcade"))
    {
        // Grip you cannot run out of, slides that end themselves.
        tune(config.tireGrip.slipAngleLimit, compoundManaged, 1.15f, 16.0f);
        config.tireGrip.slideGripLoss = (std::min)(config.tireGrip.slideGripLoss, 0.35f);
        config.tireGrip.recoveryRate = (std::max)(config.tireGrip.recoveryRate, 9.0f);
        config.tireGrip.minTractionScale = (std::max)(config.tireGrip.minTractionScale, 0.62f);
        config.tireGrip.combinedSlip = false;

        config.tireDynamics.gripRecoveryRate = (std::max)(config.tireDynamics.gripRecoveryRate, 2.8f);
        config.tireDynamics.velocityAlignmentRate = (std::max)(config.tireDynamics.velocityAlignmentRate, 2.3f);

        // Weight transfer barely costs the rear axle anything, so lifting or
        // braking mid-corner never bites.
        config.tireDynamics.liftOffRearGripLoss = (std::min)(config.tireDynamics.liftOffRearGripLoss, 0.22f);
        config.tireDynamics.brakeRearGripLoss = (std::min)(config.tireDynamics.brakeRearGripLoss, 0.28f);
        config.tireDynamics.throttleRearGripLoss = (std::min)(config.tireDynamics.throttleRearGripLoss, 0.12f);
        config.tireDynamics.overSpeedGripLoss = (std::min)(config.tireDynamics.overSpeedGripLoss, 0.22f);

        // The car supplies the correction the driver did not.
        config.tireDynamics.counterSteerTorque = (std::max)(config.tireDynamics.counterSteerTorque, 0.55f);
        config.tireDynamics.frontSlipYawDamping = (std::max)(config.tireDynamics.frontSlipYawDamping, 0.75f);

        config.yawDynamics.maxYawRate = (std::min)(config.yawDynamics.maxYawRate, 145.0f);
        config.yawDynamics.spinSlipAngle = (std::max)(config.yawDynamics.spinSlipAngle, 42.0f);
        config.yawDynamics.spinRecovery = (std::max)(config.yawDynamics.spinRecovery, 4.2f);
        config.yawDynamics.physicalYaw = false;

        // Flat pull in every gear: hold the throttle and go, and the box
        // picks the gear for you.
        config.arcadeHandling.engineDrivenAcceleration = false;
        config.transmission.mode = TransmissionConfig::Mode::Automatic;
    }
    else if (presetEquals(simulationLevel, "Simcade"))
    {
        // Reachable limit, but the car still meets the driver halfway.
        tune(config.tireGrip.lateralGrip, compoundManaged, 0.72f, 5.4f);
        config.tireGrip.slideGripLoss = (std::max)(config.tireGrip.slideGripLoss, 0.50f);
        config.tireGrip.minTractionScale = (std::min)(config.tireGrip.minTractionScale, 0.46f);
        config.tireGrip.combinedSlip = false;

        config.tireDynamics.gripRecoveryRate = (std::min)(config.tireDynamics.gripRecoveryRate, 1.6f);
        config.tireDynamics.velocityAlignmentRate = (std::min)(config.tireDynamics.velocityAlignmentRate, 1.25f);
        config.tireDynamics.liftOffRearGripLoss = (std::max)(config.tireDynamics.liftOffRearGripLoss, 0.40f);
        config.tireDynamics.throttleRearGripLoss = (std::max)(config.tireDynamics.throttleRearGripLoss, 0.22f);
        config.tireDynamics.overSpeedGripLoss = (std::max)(config.tireDynamics.overSpeedGripLoss, 0.38f);

        config.yawDynamics.maxYawRate = (std::max)(config.yawDynamics.maxYawRate, 170.0f);
        config.yawDynamics.spinRecovery = (std::min)(config.yawDynamics.spinRecovery, 2.2f + assist * 3.6f);
        config.yawDynamics.physicalYaw = false;

        config.arcadeHandling.engineDrivenAcceleration = false;
    }
    else if (presetEquals(simulationLevel, "Simulation"))
    {
        // A limit the driver reaches on any fast corner, so the car has to be
        // slowed down for one instead of steered through it.
        tune(config.tireGrip.lateralGrip, compoundManaged, 0.46f, 3.6f);
        tune(config.tireGrip.longitudinalGrip, compoundManaged, 0.86f, 0.62f);
        tune(config.tireGrip.slipAngleLimit, compoundManaged, 0.72f, 11.0f);
        config.tireGrip.slideGripLoss = (std::max)(config.tireGrip.slideGripLoss, 0.62f);
        config.tireGrip.recoveryRate = (std::min)(config.tireGrip.recoveryRate, 4.5f);
        config.tireGrip.minTractionScale = (std::min)(config.tireGrip.minTractionScale, 0.34f);

        // One friction budget for stopping and turning, so the corner has
        // to be arrived at slowed down rather than braked through.
        config.tireGrip.combinedSlip = true;

        // Slip arrives fast and leaves slowly, and the chassis no longer
        // walks itself back onto its velocity vector.
        config.tireDynamics.lateralRelaxationRate = (std::max)(config.tireDynamics.lateralRelaxationRate, 3.1f);
        config.tireDynamics.gripRecoveryRate = (std::min)(config.tireDynamics.gripRecoveryRate, 0.9f);
        // Crushing this was how the old model made slides persist, and it cost
        // the car its normal cornering: the velocity lagged the heading so far
        // that a GT car sat at seven degrees of slip just going round a corner
        // on full grip. With the tyres deciding rotation, a slide persists
        // because the rear axle is saturated, so the path is allowed to follow
        // the nose again when it is not.
        config.tireDynamics.velocityAlignmentRate = (std::min)(config.tireDynamics.velocityAlignmentRate, 1.25f);
        config.tireDynamics.maxSideSlipSpeedScale = (std::max)(config.tireDynamics.maxSideSlipSpeedScale, 0.30f);

        // Weight transfer costs the rear axle real grip: lift, brake or feed
        // the power in mid-corner and the back steps out.
        config.tireDynamics.liftOffRearGripLoss = (std::max)(config.tireDynamics.liftOffRearGripLoss, 0.62f);
        config.tireDynamics.brakeRearGripLoss = (std::max)(config.tireDynamics.brakeRearGripLoss, 0.66f);
        config.tireDynamics.throttleRearGripLoss = (std::max)(config.tireDynamics.throttleRearGripLoss, 0.34f);
        config.tireDynamics.handbrakeRearGripLoss = (std::max)(config.tireDynamics.handbrakeRearGripLoss, 0.85f);
        config.tireDynamics.overSpeedGripLoss = (std::max)(config.tireDynamics.overSpeedGripLoss, 0.55f);
        config.tireDynamics.brakeYawInstability = (std::max)(config.tireDynamics.brakeYawInstability, 0.55f);

        // Rotation builds on rear slip and is barely damped. Catching it is
        // the driver job now, not the car.
        tune(config.tireDynamics.yawFromRearSlip, balanceManaged, 1.25f, 78.0f);
        tune(config.tireDynamics.yawDrag, balanceManaged, 0.78f, 2.1f);
        config.tireDynamics.rearSlipYawTorque = (std::max)(config.tireDynamics.rearSlipYawTorque, 1.45f);
        config.tireDynamics.counterSteerTorque = (std::min)(config.tireDynamics.counterSteerTorque, 0.10f + assist * 0.52f);
        config.tireDynamics.frontSlipYawDamping = (std::min)(config.tireDynamics.frontSlipYawDamping, 0.22f + assist * 0.60f);

        tune(config.yawDynamics.slipYawResponse, balanceManaged, 1.30f, 54.0f);
        tune(config.yawDynamics.yawDamping, balanceManaged, 0.80f, 4.8f);
        config.yawDynamics.maxYawRate = (std::max)(config.yawDynamics.maxYawRate, 200.0f);
        config.yawDynamics.throttleRearSlipBoost = (std::max)(config.yawDynamics.throttleRearSlipBoost, 0.45f);
        config.yawDynamics.sideSlipToYaw = (std::max)(config.yawDynamics.sideSlipToYaw, 0.52f);
        config.yawDynamics.spinSlipAngle = (std::min)(config.yawDynamics.spinSlipAngle, 26.0f);
        config.yawDynamics.spinYawBoost = (std::max)(config.yawDynamics.spinYawBoost, 1.70f);
        config.yawDynamics.spinRecovery = (std::min)(config.yawDynamics.spinRecovery, 1.10f + assist * 3.6f);

        // Rotation comes from the tyres, so a slide has to be caught with
        // the right amount of lock rather than any lock at all.
        config.yawDynamics.physicalYaw = true;

        // The gearbox stops being decoration: come out of a corner off the
        // torque band and the engine will not pull you out of it, and the
        // driver is the one choosing the gear.
        config.arcadeHandling.engineDrivenAcceleration = true;
        config.transmission.mode = TransmissionConfig::Mode::Manual;
    }
}

ResolvedWheelTireConfig resolveWheelTire(const VehicleConfig &config, const WheelConfig &wheel)
{
    if (!config.wheelTire.enabled || wheel.overrideTire)
    {
        return ResolvedWheelTireConfig{
            wheel.gripFactor,
            wheel.longitudinalStiffness,
            wheel.lateralStiffness};
    }

    const float axleGripScale = wheel.mountPosition.y >= 0.0f
        ? config.wheelTire.frontGripScale
        : config.wheelTire.rearGripScale;
    const float scale = axleGripScale > 0.0f ? axleGripScale : 0.0f;
    return ResolvedWheelTireConfig{
        config.wheelTire.gripFactor * scale,
        config.wheelTire.longitudinalStiffness * scale,
        config.wheelTire.lateralStiffness * scale};
}

float sampleTorqueCurve(const EngineConfig &config, float rpm)
{
    if (config.torqueCurve.empty())
    {
        return 0.0f;
    }
    if (rpm <= config.torqueCurve.front().rpm)
    {
        return config.torqueCurve.front().torque;
    }
    if (rpm >= config.torqueCurve.back().rpm)
    {
        return config.torqueCurve.back().torque;
    }
    for (std::size_t i = 1; i < config.torqueCurve.size(); ++i)
    {
        const auto &prev = config.torqueCurve[i - 1];
        const auto &next = config.torqueCurve[i];
        if (rpm <= next.rpm)
        {
            float t = (rpm - prev.rpm) / (next.rpm - prev.rpm);
            return prev.torque + t * (next.torque - prev.torque);
        }
    }
    return config.torqueCurve.back().torque;
}

} // namespace raceman::physics

