#include "EngineSoundProfile.h"

#include "../physics/SimpleJson.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace raceman {

namespace fs = std::filesystem;
namespace json = raceman::physics::json;

namespace {

std::string EscapeJson(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
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

void ReadBool(const json::Object& obj, const char* key, bool& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_bool()) {
        out = it->second.as_bool();
    }
}

void ReadString(const json::Object& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_string()) {
        out = it->second.as_string();
    }
}

void ReadCurve(const json::Object& obj, const char* key, std::vector<EngineCurvePoint>& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.is_array()) {
        return;
    }
    out.clear();
    for (const json::Value& pointValue : it->second.as_array()) {
        if (!pointValue.is_object()) continue;
        EngineCurvePoint point;
        ReadNumber(pointValue.as_object(), "rpm", point.rpm);
        ReadNumber(pointValue.as_object(), "value", point.value);
        out.push_back(point);
    }
}

void WriteCurve(std::ostream& file, const char* key, const std::vector<EngineCurvePoint>& curve,
                const char* indent, bool trailingComma) {
    file << indent << "\"" << key << "\": [";
    for (std::size_t i = 0; i < curve.size(); ++i) {
        file << "{\"rpm\": " << curve[i].rpm << ", \"value\": " << curve[i].value << "}";
        if (i + 1 < curve.size()) file << ", ";
    }
    file << "]" << (trailingComma ? "," : "") << "\n";
}

// Evenly spaced firing across the 720 degree cycle, alternating banks the way a
// V engine does (cylinders 1,3,5.. on the left bank, 2,4,6.. on the right).
std::vector<EngineCylinder> EvenFiring(int cylinderCount, int bankCount) {
    std::vector<EngineCylinder> cylinders;
    const float step = 720.0f / static_cast<float>((std::max)(1, cylinderCount));
    for (int i = 0; i < cylinderCount; ++i) {
        EngineCylinder cylinder;
        cylinder.fireAngleDeg = step * static_cast<float>(i);
        cylinder.bankId = (bankCount > 1) ? (i % bankCount) : 0;
        cylinder.gain = 1.0f;
        cylinder.timingJitter = 0.6f;
        cylinders.push_back(cylinder);
    }
    return cylinders;
}

} // namespace

// -------------------------------------------------------------------------

float SampleEngineCurve(const std::vector<EngineCurvePoint>& curve, float rpm, float fallback) {
    if (curve.empty()) {
        return fallback;
    }
    if (rpm <= curve.front().rpm) {
        return curve.front().value;
    }
    if (rpm >= curve.back().rpm) {
        return curve.back().value;
    }
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const EngineCurvePoint& prev = curve[i - 1];
        const EngineCurvePoint& next = curve[i];
        if (rpm <= next.rpm) {
            const float span = next.rpm - prev.rpm;
            if (span <= 0.0f) return next.value;
            const float t = (rpm - prev.rpm) / span;
            return prev.value + t * (next.value - prev.value);
        }
    }
    return curve.back().value;
}

const char* EngineLayoutPresetName(EngineLayoutPreset preset) {
    switch (preset) {
        case EngineLayoutPreset::I3:           return "Inline 3";
        case EngineLayoutPreset::I4:           return "Inline 4";
        case EngineLayoutPreset::I5:           return "Inline 5";
        case EngineLayoutPreset::I6:           return "Inline 6";
        case EngineLayoutPreset::Flat4:        return "Flat 4";
        case EngineLayoutPreset::Flat6:        return "Flat 6";
        case EngineLayoutPreset::V6:           return "V6 (60 deg)";
        case EngineLayoutPreset::V8CrossPlane: return "V8 cross-plane";
        case EngineLayoutPreset::V8FlatPlane:  return "V8 flat-plane";
        case EngineLayoutPreset::V10:          return "V10";
        case EngineLayoutPreset::V12:          return "V12";
        case EngineLayoutPreset::VTwin90:      return "V-twin (90 deg)";
    }
    return "Unknown";
}

std::vector<EngineCylinder> MakeEngineLayout(EngineLayoutPreset preset) {
    switch (preset) {
        case EngineLayoutPreset::I3:    return EvenFiring(3, 1);
        case EngineLayoutPreset::I4:    return EvenFiring(4, 1);
        case EngineLayoutPreset::I5:    return EvenFiring(5, 1);
        case EngineLayoutPreset::I6:    return EvenFiring(6, 1);
        case EngineLayoutPreset::Flat4: return EvenFiring(4, 2);
        case EngineLayoutPreset::Flat6: return EvenFiring(6, 2);
        case EngineLayoutPreset::V6:    return EvenFiring(6, 2);
        case EngineLayoutPreset::V10:   return EvenFiring(10, 2);
        case EngineLayoutPreset::V12:   return EvenFiring(12, 2);

        case EngineLayoutPreset::V8FlatPlane: {
            // Both banks fire evenly every 180 degrees. Even banks are why a
            // flat-plane V8 screams like two inline-4s instead of burbling.
            std::vector<EngineCylinder> cylinders;
            const float leftBank[4]  = {0.0f, 180.0f, 360.0f, 540.0f};
            const float rightBank[4] = {90.0f, 270.0f, 450.0f, 630.0f};
            for (float angle : leftBank)  cylinders.push_back({angle, 0, 1.0f, 0.6f});
            for (float angle : rightBank) cylinders.push_back({angle, 1, 1.0f, 0.6f});
            return cylinders;
        }

        case EngineLayoutPreset::V8CrossPlane: {
            // Identical global firing sequence to the flat-plane above, but the
            // cross-plane crank splits it unevenly between banks: one bank sees
            // 0/180/270/450. Each bank has its own exhaust runner, so that
            // lopsided spacing is what produces the American V8 burble. This
            // one table is the single biggest character difference in the whole
            // synthesiser.
            std::vector<EngineCylinder> cylinders;
            const float leftBank[4]  = {0.0f, 180.0f, 270.0f, 450.0f};
            const float rightBank[4] = {90.0f, 360.0f, 540.0f, 630.0f};
            for (float angle : leftBank)  cylinders.push_back({angle, 0, 1.0f, 0.6f});
            for (float angle : rightBank) cylinders.push_back({angle, 1, 1.0f, 0.6f});
            return cylinders;
        }

        case EngineLayoutPreset::VTwin90: {
            std::vector<EngineCylinder> cylinders;
            cylinders.push_back({0.0f,   0, 1.0f, 1.2f});
            cylinders.push_back({270.0f, 1, 1.0f, 1.2f});
            return cylinders;
        }
    }
    return EvenFiring(4, 1);
}

std::vector<EngineOrder> MakeDefaultOrderBank(int cylinderCount, float redlineRpm) {
    // The dominant order of a four-stroke is cylinders/2: one firing event per
    // two revolutions per cylinder. Half orders below it carry the lope.
    const float dominant = (std::max)(1.0f, static_cast<float>(cylinderCount) * 0.5f);
    const float orders[] = {
        0.5f, 1.0f, 1.5f, 2.0f, 3.0f,
        dominant * 0.5f, dominant, dominant * 1.5f,
        dominant * 2.0f, dominant * 3.0f,
    };

    const float idle = 900.0f;
    const float mid  = (std::max)(2000.0f, redlineRpm * 0.55f);
    const float top  = (std::max)(mid + 500.0f, redlineRpm);

    std::vector<EngineOrder> bank;
    for (float order : orders) {
        EngineOrder entry;
        entry.order = order;

        // Dominant orders carry the note; the rest fill in body and grit.
        const bool isDominant = std::fabs(order - dominant) < 0.01f;
        const float peak = isDominant ? 1.0f : (order < dominant ? 0.45f : 0.25f);
        // Higher orders build with revs; low ones fade as the engine comes on cam.
        const float lowEnd = (order < dominant) ? peak : peak * 0.25f;

        entry.gainOnLoad = {
            {idle, lowEnd},
            {mid,  peak * 0.85f},
            {top,  peak},
        };
        // Off load everything thins out, high orders most of all: that is the
        // difference between pulling and coasting.
        entry.gainOffLoad = {
            {idle, lowEnd * 0.55f},
            {mid,  peak * 0.30f},
            {top,  peak * 0.22f},
        };
        bank.push_back(entry);
    }
    return bank;
}

// -------------------------------------------------------------------------

EngineSoundProfile EngineSoundProfileLoader::makeDefault() {
    EngineSoundProfile profile;
    profile.name = "default";
    profile.strokes = 4;
    profile.cylinders = MakeEngineLayout(EngineLayoutPreset::I4);
    profile.orders = MakeDefaultOrderBank(4, 7000.0f);
    return profile;
}

EngineSoundProfile EngineSoundProfileLoader::fromLegacyProfile(const VehicleSoundProfile& legacy,
                                                               EngineLayoutPreset preset) {
    EngineSoundProfile profile = makeDefault();
    profile.name = legacy.name;
    profile.cylinders = MakeEngineLayout(preset);
    profile.orders = MakeDefaultOrderBank(static_cast<int>(profile.cylinders.size()), 7000.0f);
    profile.mix.masterVolume = legacy.masterVolume;
    profile.spatialBlend = legacy.spatialBlend;
    profile.minDistance = legacy.minDistance;
    profile.maxDistance = legacy.maxDistance;

    // Engine layers are dropped - they are exactly what the synth replaces -
    // but the discrete one-shots are still worth keeping.
    for (const VehicleSoundTriggerEntry& trigger : legacy.triggerSounds) {
        if (trigger.trigger == VehicleSoundTrigger::Backfire) {
            continue; // now synthesised through the exhaust
        }
        profile.triggerSounds.push_back(trigger);
    }
    return profile;
}

EngineSoundProfile EngineSoundProfileLoader::loadFromFile(const std::string& path) {
    EngineSoundProfile profile = makeDefault();

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
    ReadNumber(obj, "strokes", profile.strokes);
    ReadNumber(obj, "idleInstability", profile.idleInstability);
    ReadNumber(obj, "combustionVariance", profile.combustionVariance);
    ReadNumber(obj, "combustionDurationMs", profile.combustionDurationMs);
    ReadNumber(obj, "combustionNoise", profile.combustionNoise);
    ReadNumber(obj, "spatialBlend", profile.spatialBlend);
    ReadNumber(obj, "minDistance", profile.minDistance);
    ReadNumber(obj, "maxDistance", profile.maxDistance);

    if (auto it = obj.find("cylinders"); it != obj.end() && it->second.is_array()) {
        profile.cylinders.clear();
        for (const json::Value& value : it->second.as_array()) {
            if (!value.is_object()) continue;
            EngineCylinder cylinder;
            ReadNumber(value.as_object(), "fireAngleDeg", cylinder.fireAngleDeg);
            ReadNumber(value.as_object(), "bankId", cylinder.bankId);
            ReadNumber(value.as_object(), "gain", cylinder.gain);
            ReadNumber(value.as_object(), "timingJitter", cylinder.timingJitter);
            profile.cylinders.push_back(cylinder);
        }
    }

    if (auto it = obj.find("exhaust"); it != obj.end() && it->second.is_object()) {
        const json::Object& e = it->second.as_object();
        ReadNumber(e, "runnerLengthM", profile.exhaust.runnerLengthM);
        ReadNumber(e, "collectorLengthM", profile.exhaust.collectorLengthM);
        ReadNumber(e, "gasSpeedMs", profile.exhaust.gasSpeedMs);
        ReadNumber(e, "runnerReflection", profile.exhaust.runnerReflection);
        ReadNumber(e, "runnerDamping", profile.exhaust.runnerDamping);
        ReadNumber(e, "mufflerStages", profile.exhaust.mufflerStages);
        ReadNumber(e, "mufflerLengthM", profile.exhaust.mufflerLengthM);
        ReadNumber(e, "mufflerReflection", profile.exhaust.mufflerReflection);
        ReadNumber(e, "tailpipeBrightness", profile.exhaust.tailpipeBrightness);
    }

    if (auto it = obj.find("intake"); it != obj.end() && it->second.is_object()) {
        const json::Object& i = it->second.as_object();
        ReadNumber(i, "lengthM", profile.intake.lengthM);
        ReadNumber(i, "airSpeedMs", profile.intake.airSpeedMs);
        ReadNumber(i, "reflection", profile.intake.reflection);
        ReadNumber(i, "damping", profile.intake.damping);
        ReadNumber(i, "noise", profile.intake.noise);
        ReadNumber(i, "pulseGain", profile.intake.pulseGain);
    }

    if (auto it = obj.find("orders"); it != obj.end() && it->second.is_array()) {
        profile.orders.clear();
        for (const json::Value& value : it->second.as_array()) {
            if (!value.is_object()) continue;
            EngineOrder order;
            ReadNumber(value.as_object(), "order", order.order);
            ReadCurve(value.as_object(), "gainOnLoad", order.gainOnLoad);
            ReadCurve(value.as_object(), "gainOffLoad", order.gainOffLoad);
            profile.orders.push_back(order);
        }
    }

    if (auto it = obj.find("turbo"); it != obj.end() && it->second.is_object()) {
        const json::Object& t = it->second.as_object();
        ReadBool(t, "enabled", profile.turbo.enabled);
        ReadBool(t, "supercharger", profile.turbo.supercharger);
        ReadNumber(t, "whistleRatio", profile.turbo.whistleRatio);
        ReadNumber(t, "whistleGain", profile.turbo.whistleGain);
        ReadNumber(t, "whistleResonance", profile.turbo.whistleResonance);
        ReadNumber(t, "blowOffGain", profile.turbo.blowOffGain);
        ReadNumber(t, "blowOffDecaySeconds", profile.turbo.blowOffDecaySeconds);
    }

    if (auto it = obj.find("drivetrain"); it != obj.end() && it->second.is_object()) {
        const json::Object& d = it->second.as_object();
        ReadNumber(d, "whineGain", profile.drivetrain.whineGain);
        ReadNumber(d, "whineRatio", profile.drivetrain.whineRatio);
        ReadNumber(d, "whineResonance", profile.drivetrain.whineResonance);
        ReadNumber(d, "shiftCutDepth", profile.drivetrain.shiftCutDepth);
    }

    if (auto it = obj.find("overrun"); it != obj.end() && it->second.is_object()) {
        const json::Object& o = it->second.as_object();
        ReadBool(o, "enabled", profile.overrun.enabled);
        ReadNumber(o, "minRpm", profile.overrun.minRpm);
        ReadNumber(o, "density", profile.overrun.density);
        ReadNumber(o, "gain", profile.overrun.gain);
        ReadNumber(o, "limiterPopGain", profile.overrun.limiterPopGain);
    }

    if (auto it = obj.find("noise"); it != obj.end() && it->second.is_object()) {
        const json::Object& n = it->second.as_object();
        ReadNumber(n, "valvetrainGain", profile.noise.valvetrainGain);
        ReadNumber(n, "valvetrainFreq", profile.noise.valvetrainFreq);
        ReadNumber(n, "valvetrainQ", profile.noise.valvetrainQ);
    }

    if (auto it = obj.find("mix"); it != obj.end() && it->second.is_object()) {
        const json::Object& m = it->second.as_object();
        ReadNumber(m, "exhaustGain", profile.mix.exhaustGain);
        ReadNumber(m, "intakeGain", profile.mix.intakeGain);
        ReadNumber(m, "blockGain", profile.mix.blockGain);
        ReadNumber(m, "drive", profile.mix.drive);
        ReadNumber(m, "masterVolume", profile.mix.masterVolume);
    }

    if (auto it = obj.find("triggerSounds"); it != obj.end() && it->second.is_array()) {
        profile.triggerSounds.clear();
        for (const json::Value& value : it->second.as_array()) {
            if (!value.is_object()) continue;
            const json::Object& t = value.as_object();
            VehicleSoundTriggerEntry entry;
            ReadString(t, "clipPath", entry.clipPath);
            ReadNumber(t, "volume", entry.volume);
            ReadNumber(t, "minRpmForBackfire", entry.minRpmForBackfire);
            ReadNumber(t, "minLateralSpeedForSqueal", entry.minLateralSpeedForSqueal);
            std::string triggerName;
            ReadString(t, "trigger", triggerName);
            if (triggerName == "gearUp")           entry.trigger = VehicleSoundTrigger::GearUp;
            else if (triggerName == "gearDown")    entry.trigger = VehicleSoundTrigger::GearDown;
            else if (triggerName == "engineStart") entry.trigger = VehicleSoundTrigger::EngineStart;
            else if (triggerName == "engineStop")  entry.trigger = VehicleSoundTrigger::EngineStop;
            else if (triggerName == "tireSqueal")  entry.trigger = VehicleSoundTrigger::TireSqueal;
            else continue;
            if (!entry.clipPath.empty()) {
                profile.triggerSounds.push_back(entry);
            }
        }
    }

    return profile;
}

bool EngineSoundProfileLoader::saveToFile(const std::string& path, const EngineSoundProfile& profile,
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
    file << "  \"strokes\": " << profile.strokes << ",\n";
    file << "  \"idleInstability\": " << profile.idleInstability << ",\n";
    file << "  \"combustionVariance\": " << profile.combustionVariance << ",\n";
    file << "  \"combustionDurationMs\": " << profile.combustionDurationMs << "," << "\n";
    file << "  \"combustionNoise\": " << profile.combustionNoise << "," << "\n";
    file << "  \"spatialBlend\": " << profile.spatialBlend << ",\n";
    file << "  \"minDistance\": " << profile.minDistance << ",\n";
    file << "  \"maxDistance\": " << profile.maxDistance << ",\n";

    file << "  \"cylinders\": [\n";
    for (std::size_t i = 0; i < profile.cylinders.size(); ++i) {
        const EngineCylinder& c = profile.cylinders[i];
        file << "    {\"fireAngleDeg\": " << c.fireAngleDeg
             << ", \"bankId\": " << c.bankId
             << ", \"gain\": " << c.gain
             << ", \"timingJitter\": " << c.timingJitter << "}"
             << (i + 1 < profile.cylinders.size() ? "," : "") << "\n";
    }
    file << "  ],\n";

    const EngineExhaustSettings& e = profile.exhaust;
    file << "  \"exhaust\": {\n";
    file << "    \"runnerLengthM\": " << e.runnerLengthM << ",\n";
    file << "    \"collectorLengthM\": " << e.collectorLengthM << ",\n";
    file << "    \"gasSpeedMs\": " << e.gasSpeedMs << ",\n";
    file << "    \"runnerReflection\": " << e.runnerReflection << ",\n";
    file << "    \"runnerDamping\": " << e.runnerDamping << ",\n";
    file << "    \"mufflerStages\": " << e.mufflerStages << ",\n";
    file << "    \"mufflerLengthM\": " << e.mufflerLengthM << ",\n";
    file << "    \"mufflerReflection\": " << e.mufflerReflection << ",\n";
    file << "    \"tailpipeBrightness\": " << e.tailpipeBrightness << "\n";
    file << "  },\n";

    const EngineIntakeSettings& i = profile.intake;
    file << "  \"intake\": {\n";
    file << "    \"lengthM\": " << i.lengthM << ",\n";
    file << "    \"airSpeedMs\": " << i.airSpeedMs << ",\n";
    file << "    \"reflection\": " << i.reflection << ",\n";
    file << "    \"damping\": " << i.damping << ",\n";
    file << "    \"noise\": " << i.noise << ",\n";
    file << "    \"pulseGain\": " << i.pulseGain << "\n";
    file << "  },\n";

    file << "  \"orders\": [\n";
    for (std::size_t oi = 0; oi < profile.orders.size(); ++oi) {
        const EngineOrder& order = profile.orders[oi];
        file << "    {\n";
        file << "      \"order\": " << order.order << ",\n";
        WriteCurve(file, "gainOnLoad", order.gainOnLoad, "      ", true);
        WriteCurve(file, "gainOffLoad", order.gainOffLoad, "      ", false);
        file << "    }" << (oi + 1 < profile.orders.size() ? "," : "") << "\n";
    }
    file << "  ],\n";

    const EngineTurboSettings& t = profile.turbo;
    file << "  \"turbo\": {\n";
    file << "    \"enabled\": " << (t.enabled ? "true" : "false") << ",\n";
    file << "    \"supercharger\": " << (t.supercharger ? "true" : "false") << ",\n";
    file << "    \"whistleRatio\": " << t.whistleRatio << ",\n";
    file << "    \"whistleGain\": " << t.whistleGain << ",\n";
    file << "    \"whistleResonance\": " << t.whistleResonance << ",\n";
    file << "    \"blowOffGain\": " << t.blowOffGain << ",\n";
    file << "    \"blowOffDecaySeconds\": " << t.blowOffDecaySeconds << "\n";
    file << "  },\n";

    const EngineDrivetrainSettings& d = profile.drivetrain;
    file << "  \"drivetrain\": {\n";
    file << "    \"whineGain\": " << d.whineGain << ",\n";
    file << "    \"whineRatio\": " << d.whineRatio << ",\n";
    file << "    \"whineResonance\": " << d.whineResonance << ",\n";
    file << "    \"shiftCutDepth\": " << d.shiftCutDepth << "\n";
    file << "  },\n";

    const EngineOverrunSettings& o = profile.overrun;
    file << "  \"overrun\": {\n";
    file << "    \"enabled\": " << (o.enabled ? "true" : "false") << ",\n";
    file << "    \"minRpm\": " << o.minRpm << ",\n";
    file << "    \"density\": " << o.density << ",\n";
    file << "    \"gain\": " << o.gain << ",\n";
    file << "    \"limiterPopGain\": " << o.limiterPopGain << "\n";
    file << "  },\n";

    const EngineNoiseSettings& n = profile.noise;
    file << "  \"noise\": {\n";
    file << "    \"valvetrainGain\": " << n.valvetrainGain << ",\n";
    file << "    \"valvetrainFreq\": " << n.valvetrainFreq << ",\n";
    file << "    \"valvetrainQ\": " << n.valvetrainQ << "\n";
    file << "  },\n";

    const EngineBodySettings& bd = profile.body;
    file << "  \"body\": {\n";
    file << "    \"resonance1Hz\": " << bd.resonance1Hz << ",\n";
    file << "    \"resonance1Q\": " << bd.resonance1Q << ",\n";
    file << "    \"resonance1Gain\": " << bd.resonance1Gain << ",\n";
    file << "    \"resonance2Hz\": " << bd.resonance2Hz << ",\n";
    file << "    \"resonance2Q\": " << bd.resonance2Q << ",\n";
    file << "    \"resonance2Gain\": " << bd.resonance2Gain << ",\n";
    file << "    \"subGain\": " << bd.subGain << ",\n";
    file << "    \"subCutoffHz\": " << bd.subCutoffHz << ",\n";
    file << "    \"toneTilt\": " << bd.toneTilt << "\n";
    file << "  },\n";

    const EngineMixSettings& m = profile.mix;
    file << "  \"mix\": {\n";
    file << "    \"exhaustGain\": " << m.exhaustGain << ",\n";
    file << "    \"intakeGain\": " << m.intakeGain << ",\n";
    file << "    \"blockGain\": " << m.blockGain << ",\n";
    file << "    \"drive\": " << m.drive << ",\n";
    file << "    \"masterVolume\": " << m.masterVolume << "\n";
    file << "  },\n";

    file << "  \"triggerSounds\": [\n";
    for (std::size_t ti = 0; ti < profile.triggerSounds.size(); ++ti) {
        const VehicleSoundTriggerEntry& trigger = profile.triggerSounds[ti];
        const char* name = "gearUp";
        switch (trigger.trigger) {
            case VehicleSoundTrigger::GearUp:      name = "gearUp"; break;
            case VehicleSoundTrigger::GearDown:    name = "gearDown"; break;
            case VehicleSoundTrigger::EngineStart: name = "engineStart"; break;
            case VehicleSoundTrigger::EngineStop:  name = "engineStop"; break;
            case VehicleSoundTrigger::TireSqueal:  name = "tireSqueal"; break;
            case VehicleSoundTrigger::Backfire:    name = "gearUp"; break; // now procedural
        }
        file << "    {\"clipPath\": " << EscapeJson(trigger.clipPath)
             << ", \"trigger\": \"" << name << "\""
             << ", \"volume\": " << trigger.volume
             << ", \"minRpmForBackfire\": " << trigger.minRpmForBackfire
             << ", \"minLateralSpeedForSqueal\": " << trigger.minLateralSpeedForSqueal << "}"
             << (ti + 1 < profile.triggerSounds.size() ? "," : "") << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    return file.good();
}

} // namespace raceman
