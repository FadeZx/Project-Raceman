#include "VehicleSoundProfile.h"

#include "../physics/SimpleJson.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace raceman {

using raceman::physics::json::Value;
using raceman::physics::json::Object;
namespace json = raceman::physics::json;

namespace {

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char ch : s) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out.push_back(ch); break;
        }
    }
    out.push_back('"');
    return out;
}

const char* TriggerToString(VehicleSoundTrigger t) {
    switch (t) {
    case VehicleSoundTrigger::GearUp:      return "gearUp";
    case VehicleSoundTrigger::GearDown:    return "gearDown";
    case VehicleSoundTrigger::Backfire:    return "backfire";
    case VehicleSoundTrigger::EngineStart: return "engineStart";
    case VehicleSoundTrigger::EngineStop:  return "engineStop";
    case VehicleSoundTrigger::TireSqueal:  return "tireSqueal";
    default: return "gearUp";
    }
}

VehicleSoundTrigger TriggerFromString(const std::string& s) {
    if (s == "gearDown")    return VehicleSoundTrigger::GearDown;
    if (s == "backfire")    return VehicleSoundTrigger::Backfire;
    if (s == "engineStart") return VehicleSoundTrigger::EngineStart;
    if (s == "engineStop")  return VehicleSoundTrigger::EngineStop;
    if (s == "tireSqueal")  return VehicleSoundTrigger::TireSqueal;
    return VehicleSoundTrigger::GearUp;
}

template<typename T>
void ReadNumber(const json::Object& obj, const char* key, T& out) {
    auto it = obj.find(key);
    if (it != obj.end()) out = static_cast<T>(it->second.as_number());
}

void ReadString(const json::Object& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_string()) out = it->second.as_string();
}

void ReadBool(const json::Object& obj, const char* key, bool& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_bool()) out = it->second.as_bool();
}

} // namespace

VehicleSoundProfile VehicleSoundProfileLoader::makeDefault() {
    VehicleSoundProfile p;
    p.name = "default";

    VehicleSoundEngineLayer idle;
    idle.clipPath         = "assets/audio/engine_idle.ogg";
    idle.rpmMin           = 0.0f;
    idle.rpmMax           = 3000.0f;
    idle.pitchAtRpmMin    = 0.7f;
    idle.pitchAtRpmMax    = 1.1f;
    idle.volumeAtRpmMin   = 0.5f;
    idle.volumeAtRpmMax   = 0.0f;
    p.engineLayers.push_back(idle);

    VehicleSoundEngineLayer mid;
    mid.clipPath             = "assets/audio/engine_mid.ogg";
    mid.rpmMin               = 1000.0f;
    mid.rpmMax               = 7000.0f;
    mid.pitchAtRpmMin        = 0.8f;
    mid.pitchAtRpmMax        = 1.4f;
    mid.volumeAtRpmMin       = 0.0f;
    mid.volumeAtRpmMax       = 1.0f;
    mid.volumeThrottleScale  = 0.15f;
    p.engineLayers.push_back(mid);

    VehicleSoundTriggerEntry gearUp;
    gearUp.clipPath = "assets/audio/gear_up.ogg";
    gearUp.trigger  = VehicleSoundTrigger::GearUp;
    p.triggerSounds.push_back(gearUp);

    VehicleSoundTriggerEntry gearDown;
    gearDown.clipPath = "assets/audio/gear_down.ogg";
    gearDown.trigger  = VehicleSoundTrigger::GearDown;
    p.triggerSounds.push_back(gearDown);

    return p;
}

VehicleSoundProfile VehicleSoundProfileLoader::loadFromFile(const std::string& path) {
    VehicleSoundProfile profile = makeDefault();
    profile.engineLayers.clear();
    profile.triggerSounds.clear();

    std::ifstream file(path);
    if (!file.is_open()) return profile;

    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string text = ss.str();

    json::Value root;
    try {
        root = json::parse(text);
    } catch (...) {
        return profile;
    }

    if (!root.is_object()) return profile;
    const json::Object& obj = root.as_object();

    ReadString(obj, "name",         profile.name);
    ReadNumber(obj, "masterVolume", profile.masterVolume);
    ReadNumber(obj, "spatialBlend", profile.spatialBlend);
    ReadNumber(obj, "minDistance",  profile.minDistance);
    ReadNumber(obj, "maxDistance",  profile.maxDistance);

    // Engine layers
    if (auto it = obj.find("engineLayers"); it != obj.end() && it->second.is_array()) {
        for (const json::Value& layerVal : it->second.as_array()) {
            if (!layerVal.is_object()) continue;
            const json::Object& lo = layerVal.as_object();
            VehicleSoundEngineLayer layer;
            ReadString(lo, "clipPath",            layer.clipPath);
            ReadNumber(lo, "rpmMin",              layer.rpmMin);
            ReadNumber(lo, "rpmMax",              layer.rpmMax);
            ReadNumber(lo, "pitchAtRpmMin",       layer.pitchAtRpmMin);
            ReadNumber(lo, "pitchAtRpmMax",       layer.pitchAtRpmMax);
            ReadNumber(lo, "volumeAtRpmMin",      layer.volumeAtRpmMin);
            ReadNumber(lo, "volumeAtRpmMax",      layer.volumeAtRpmMax);
            ReadNumber(lo, "volumeThrottleScale", layer.volumeThrottleScale);
            if (!layer.clipPath.empty()) profile.engineLayers.push_back(layer);
        }
    }

    // Trigger sounds
    if (auto it = obj.find("triggerSounds"); it != obj.end() && it->second.is_array()) {
        for (const json::Value& tv : it->second.as_array()) {
            if (!tv.is_object()) continue;
            const json::Object& to = tv.as_object();
            VehicleSoundTriggerEntry entry;
            ReadString(to, "clipPath", entry.clipPath);
            std::string trigStr;
            ReadString(to, "trigger", trigStr);
            entry.trigger = TriggerFromString(trigStr);
            ReadNumber(to, "volume",                   entry.volume);
            ReadNumber(to, "minRpmForBackfire",        entry.minRpmForBackfire);
            ReadNumber(to, "minLateralSpeedForSqueal", entry.minLateralSpeedForSqueal);
            if (!entry.clipPath.empty()) profile.triggerSounds.push_back(entry);
        }
    }

    return profile;
}

bool VehicleSoundProfileLoader::saveToFile(const std::string& path,
                                            const VehicleSoundProfile& profile,
                                            std::string* outError) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream file(path);
    if (!file.is_open()) {
        if (outError) *outError = "Cannot open file for writing: " + path;
        return false;
    }

    file << "{\n";
    file << "  \"name\": "          << EscapeJson(profile.name)        << ",\n";
    file << "  \"masterVolume\": "  << profile.masterVolume             << ",\n";
    file << "  \"spatialBlend\": "  << profile.spatialBlend             << ",\n";
    file << "  \"minDistance\": "   << profile.minDistance              << ",\n";
    file << "  \"maxDistance\": "   << profile.maxDistance              << ",\n";

    // Engine layers
    file << "  \"engineLayers\": [\n";
    for (std::size_t i = 0; i < profile.engineLayers.size(); ++i) {
        const auto& l = profile.engineLayers[i];
        file << "    {\n";
        file << "      \"clipPath\": "            << EscapeJson(l.clipPath)        << ",\n";
        file << "      \"rpmMin\": "              << l.rpmMin                       << ",\n";
        file << "      \"rpmMax\": "              << l.rpmMax                       << ",\n";
        file << "      \"pitchAtRpmMin\": "       << l.pitchAtRpmMin                << ",\n";
        file << "      \"pitchAtRpmMax\": "       << l.pitchAtRpmMax                << ",\n";
        file << "      \"volumeAtRpmMin\": "      << l.volumeAtRpmMin               << ",\n";
        file << "      \"volumeAtRpmMax\": "      << l.volumeAtRpmMax               << ",\n";
        file << "      \"volumeThrottleScale\": " << l.volumeThrottleScale          << "\n";
        file << "    }" << (i + 1 < profile.engineLayers.size() ? "," : "") << "\n";
    }
    file << "  ],\n";

    // Trigger sounds
    file << "  \"triggerSounds\": [\n";
    for (std::size_t i = 0; i < profile.triggerSounds.size(); ++i) {
        const auto& t = profile.triggerSounds[i];
        file << "    {\n";
        file << "      \"clipPath\": "                 << EscapeJson(t.clipPath)            << ",\n";
        file << "      \"trigger\": "                  << EscapeJson(TriggerToString(t.trigger)) << ",\n";
        file << "      \"volume\": "                   << t.volume                           << ",\n";
        file << "      \"minRpmForBackfire\": "        << t.minRpmForBackfire                << ",\n";
        file << "      \"minLateralSpeedForSqueal\": " << t.minLateralSpeedForSqueal         << "\n";
        file << "    }" << (i + 1 < profile.triggerSounds.size() ? "," : "") << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    return file.good();
}

} // namespace raceman
