#include "VehicleConfig.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace raceman::physics
{
namespace
{
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

DifferentialConfig readDifferential(const json::Value &value)
{
    DifferentialConfig differential{};
    const auto &obj = value.as_object();
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
    if (auto it = obj.find("differential"); it != obj.end())
    {
        config.differential = readDifferential(it->second);
    }
    if (auto it = obj.find("engine"); it != obj.end())
    {
        config.engine = readEngine(it->second);
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

