#include "VehicleConfig.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

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
    case TransmissionConfig::Mode::Manual: return "manual";
    case TransmissionConfig::Mode::Automatic:
    default: return "automatic";
    }
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
    stream << "  \"differential\": {\n";
    stream << "    \"torqueSplit\": " << config.differential.torqueSplit << ",\n";
    stream << "    \"lockingCoefficient\": " << config.differential.lockingCoefficient << ",\n";
    stream << "    \"limitedSlip\": " << (config.differential.limitedSlip ? "true" : "false") << "\n";
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

