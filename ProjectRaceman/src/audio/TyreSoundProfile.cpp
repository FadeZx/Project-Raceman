#include "TyreSoundProfile.h"

#include "../physics/SimpleJson.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace raceman {

namespace fs = std::filesystem;
namespace json = raceman::physics::json;

namespace {

// Must match TrackSurfaceType: Asphalt, Dirt, Grass, Curb, Wall, Custom.
constexpr int kSurfaceCount = 6;
const char* kSurfaceNames[kSurfaceCount] = {
    "asphalt", "dirt", "grass", "curb", "wall", "custom"
};

std::string EscapeJson(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            default:   out += c;      break;
        }
    }
    out += "\"";
    return out;
}

template <typename T>
void ReadNumber(const json::Object& obj, const char* key, T& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_number()) {
        out = static_cast<T>(it->second.as_number());
    }
}

void ReadString(const json::Object& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_string()) {
        out = it->second.as_string();
    }
}

} // namespace

TyreSoundProfile TyreSoundProfileLoader::makeDefault() {
    TyreSoundProfile profile;
    profile.name = "default";
    profile.surfaces.resize(kSurfaceCount);

    // Asphalt: a clean, fairly bright hiss that sings readily at the limit.
    TyreSurfaceSound& asphalt = profile.surfaces[0];
    asphalt.rollGain = 1.00f; asphalt.rollLowHz = 140.0f; asphalt.rollHighHz = 2800.0f;
    asphalt.grainAmount = 0.05f; asphalt.squealGain = 1.00f;

    // Dirt: darker, and grainy because loose material passes under the tread.
    TyreSurfaceSound& dirt = profile.surfaces[1];
    dirt.rollGain = 1.25f; dirt.rollLowHz = 90.0f; dirt.rollHighHz = 1700.0f;
    dirt.grainAmount = 0.75f; dirt.grainRateScale = 1.35f;
    dirt.squealGain = 0.18f;   // loose surfaces slide, they do not sing

    // Grass: dull, washy, and essentially incapable of squealing.
    TyreSurfaceSound& grass = profile.surfaces[2];
    grass.rollGain = 1.05f; grass.rollLowHz = 70.0f; grass.rollHighHz = 1100.0f;
    grass.grainAmount = 0.45f; grass.grainRateScale = 0.8f;
    grass.squealGain = 0.05f;

    // Kerb: periodic, not random. The rumble rate tracks road speed.
    TyreSurfaceSound& curb = profile.surfaces[3];
    curb.rollGain = 1.15f; curb.rollLowHz = 110.0f; curb.rollHighHz = 2200.0f;
    curb.grainAmount = 0.20f;
    curb.squealGain = 0.55f;
    curb.rumbleGain = 1.10f; curb.rumbleHz = 62.0f;

    // Wall: scraping contact rather than rolling.
    TyreSurfaceSound& wall = profile.surfaces[4];
    wall.rollGain = 0.85f; wall.rollLowHz = 200.0f; wall.rollHighHz = 3600.0f;
    wall.grainAmount = 0.55f; wall.squealGain = 0.85f;

    profile.surfaces[5] = asphalt; // Custom falls back to tarmac behaviour
    return profile;
}

TyreSoundProfile TyreSoundProfileLoader::loadFromFile(const std::string& path) {
    TyreSoundProfile profile = makeDefault();

    std::ifstream file(path);
    if (!file.is_open()) {
        return profile;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    json::Value root;
    try {
        root = json::parse(buffer.str());
    } catch (...) {
        return profile;
    }
    if (!root.is_object()) {
        return profile;
    }
    const json::Object& obj = root.as_object();

    ReadString(obj, "name", profile.name);
    ReadNumber(obj, "rollMasterGain", profile.rollMasterGain);
    ReadNumber(obj, "rollSpeedRefMps", profile.rollSpeedRefMps);
    ReadNumber(obj, "rollLoadInfluence", profile.rollLoadInfluence);
    ReadNumber(obj, "squealMasterGain", profile.squealMasterGain);
    ReadNumber(obj, "squealBaseHz", profile.squealBaseHz);
    ReadNumber(obj, "squealRiseHz", profile.squealRiseHz);
    ReadNumber(obj, "squealResonance", profile.squealResonance);
    ReadNumber(obj, "squealSlipThreshold", profile.squealSlipThreshold);
    ReadNumber(obj, "squealSlipFull", profile.squealSlipFull);
    ReadNumber(obj, "squealAttackSeconds", profile.squealAttackSeconds);
    ReadNumber(obj, "squealReleaseSeconds", profile.squealReleaseSeconds);
    ReadNumber(obj, "impactGain", profile.impactGain);
    ReadNumber(obj, "impactThreshold", profile.impactThreshold);
    ReadNumber(obj, "masterVolume", profile.masterVolume);
    ReadNumber(obj, "minDistance", profile.minDistance);
    ReadNumber(obj, "maxDistance", profile.maxDistance);
    ReadNumber(obj, "spatialBlend", profile.spatialBlend);

    if (auto it = obj.find("surfaces"); it != obj.end() && it->second.is_object()) {
        const json::Object& surfaces = it->second.as_object();
        for (int i = 0; i < kSurfaceCount; ++i) {
            auto entry = surfaces.find(kSurfaceNames[i]);
            if (entry == surfaces.end() || !entry->second.is_object()) continue;
            const json::Object& s = entry->second.as_object();
            TyreSurfaceSound& dst = profile.surfaces[static_cast<std::size_t>(i)];
            ReadNumber(s, "rollGain", dst.rollGain);
            ReadNumber(s, "rollLowHz", dst.rollLowHz);
            ReadNumber(s, "rollHighHz", dst.rollHighHz);
            ReadNumber(s, "grainAmount", dst.grainAmount);
            ReadNumber(s, "grainRateScale", dst.grainRateScale);
            ReadNumber(s, "squealGain", dst.squealGain);
            ReadNumber(s, "rumbleGain", dst.rumbleGain);
            ReadNumber(s, "rumbleHz", dst.rumbleHz);
        }
    }
    return profile;
}

bool TyreSoundProfileLoader::saveToFile(const std::string& path, const TyreSoundProfile& profile,
                                        std::string* outError) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    std::ofstream file(path);
    if (!file.is_open()) {
        if (outError) *outError = "Cannot open file for writing: " + path;
        return false;
    }

    file << "{\n";
    file << "  \"name\": " << EscapeJson(profile.name) << ",\n";
    file << "  \"rollMasterGain\": " << profile.rollMasterGain << ",\n";
    file << "  \"rollSpeedRefMps\": " << profile.rollSpeedRefMps << ",\n";
    file << "  \"rollLoadInfluence\": " << profile.rollLoadInfluence << ",\n";
    file << "  \"squealMasterGain\": " << profile.squealMasterGain << ",\n";
    file << "  \"squealBaseHz\": " << profile.squealBaseHz << ",\n";
    file << "  \"squealRiseHz\": " << profile.squealRiseHz << ",\n";
    file << "  \"squealResonance\": " << profile.squealResonance << ",\n";
    file << "  \"squealSlipThreshold\": " << profile.squealSlipThreshold << ",\n";
    file << "  \"squealSlipFull\": " << profile.squealSlipFull << ",\n";
    file << "  \"squealAttackSeconds\": " << profile.squealAttackSeconds << ",\n";
    file << "  \"squealReleaseSeconds\": " << profile.squealReleaseSeconds << ",\n";
    file << "  \"impactGain\": " << profile.impactGain << ",\n";
    file << "  \"impactThreshold\": " << profile.impactThreshold << ",\n";
    file << "  \"masterVolume\": " << profile.masterVolume << ",\n";
    file << "  \"minDistance\": " << profile.minDistance << ",\n";
    file << "  \"maxDistance\": " << profile.maxDistance << ",\n";
    file << "  \"spatialBlend\": " << profile.spatialBlend << ",\n";
    file << "  \"surfaces\": {\n";
    for (int i = 0; i < kSurfaceCount; ++i) {
        const TyreSurfaceSound& s = (i < static_cast<int>(profile.surfaces.size()))
            ? profile.surfaces[static_cast<std::size_t>(i)]
            : TyreSurfaceSound{};
        file << "    \"" << kSurfaceNames[i] << "\": {"
             << "\"rollGain\": " << s.rollGain
             << ", \"rollLowHz\": " << s.rollLowHz
             << ", \"rollHighHz\": " << s.rollHighHz
             << ", \"grainAmount\": " << s.grainAmount
             << ", \"grainRateScale\": " << s.grainRateScale
             << ", \"squealGain\": " << s.squealGain
             << ", \"rumbleGain\": " << s.rumbleGain
             << ", \"rumbleHz\": " << s.rumbleHz << "}"
             << (i + 1 < kSurfaceCount ? "," : "") << "\n";
    }
    file << "  }\n";
    file << "}\n";
    return file.good();
}

} // namespace raceman
