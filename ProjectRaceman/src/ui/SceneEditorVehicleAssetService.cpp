#include "SceneEditorInternal.h"
#include "SceneEditorVehicleValidation.h"
#include "../audio/VehicleSoundProfile.h"
#include "../physics/SimpleJson.h"
#include "../physics/VehicleConfig.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

namespace json = raceman::physics::json;

// Sits next to config/imgui.ini so the window layout and what the audio panels
// were showing come back from the same startup.
constexpr const char* kAudioEditorPanelStatePath = "config/audio_editor_state.json";

std::string QuoteJsonString(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    out += "\"";
    return out;
}

const json::Object* JsonSection(const json::Object& root, const char* key) {
    auto it = root.find(key);
    return (it != root.end() && it->second.is_object()) ? &it->second.as_object() : nullptr;
}

void JsonReadFloat(const json::Object& obj, const char* key, float& out, float lo, float hi) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_number()) {
        out = (std::clamp)(static_cast<float>(it->second.as_number()), lo, hi);
    }
}

void JsonReadInt(const json::Object& obj, const char* key, int& out, int lo, int hi) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_number()) {
        out = (std::clamp)(static_cast<int>(it->second.as_number()), lo, hi);
    }
}

void JsonReadBool(const json::Object& obj, const char* key, bool& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_bool()) {
        out = it->second.as_bool();
    }
}

void JsonReadString(const json::Object& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->second.is_string()) {
        out = it->second.as_string();
    }
}

// A remembered profile is only worth restoring while the asset it names is
// still on disk - projects get moved and files get deleted between sessions.
bool RememberedAssetExists(const std::string& projectPath) {
    if (projectPath.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::is_regular_file(ProjectAssetPathToAbsolute(projectPath), ec);
}

} // namespace

std::string SceneEditor::SerializeAudioEditorPanelState() const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"engineSoundEditor\": {\n";
    out << "    \"open\": " << (showEngineSoundEditor_ ? "true" : "false") << ",\n";
    out << "    \"path\": " << QuoteJsonString(inspectedEngineSoundPath_) << ",\n";
    out << "    \"tab\": " << QuoteJsonString(engineSoundEditorActiveTab_) << ",\n";
    out << "    \"selectedOrder\": " << engineSoundSelectedOrder_ << ",\n";
    out << "    \"idleRpm\": " << engineSoundAuditionIdle_ << ",\n";
    out << "    \"redlineRpm\": " << engineSoundAuditionRedline_ << ",\n";
    out << "    \"rpm\": " << engineSoundAuditionRpm_ << ",\n";
    out << "    \"throttle\": " << engineSoundAuditionThrottle_ << "\n";
    out << "  },\n";
    out << "  \"vehicleSoundEditor\": {\n";
    out << "    \"open\": " << (showVehicleSoundEditor_ ? "true" : "false") << ",\n";
    out << "    \"path\": " << QuoteJsonString(inspectedVehicleSoundPath_) << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

void SceneEditor::LoadAudioEditorPanelState() {
    std::ifstream file(kAudioEditorPanelStatePath);
    if (file.is_open()) {
        const std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        json::Value root;
        try {
            root = json::parse(content);
        } catch (const std::exception&) {
            root = json::Value{};  // a corrupt state file falls back to defaults rather than failing startup
        }

        if (root.is_object()) {
            const json::Object& obj = root.as_object();

            if (const json::Object* engine = JsonSection(obj, "engineSoundEditor")) {
                // The audition rig comes back whether or not the profile does:
                // idle and redline are how the panel was calibrated by ear.
                JsonReadFloat(*engine, "idleRpm", engineSoundAuditionIdle_, 300.0f, 3000.0f);
                JsonReadFloat(*engine, "redlineRpm", engineSoundAuditionRedline_, 2000.0f, 20000.0f);
                engineSoundAuditionRedline_ =
                    (std::max)(engineSoundAuditionIdle_ + 500.0f, engineSoundAuditionRedline_);
                JsonReadFloat(*engine, "rpm", engineSoundAuditionRpm_, engineSoundAuditionIdle_,
                              engineSoundAuditionRedline_);
                JsonReadFloat(*engine, "throttle", engineSoundAuditionThrottle_, 0.0f, 1.0f);
                JsonReadInt(*engine, "selectedOrder", engineSoundSelectedOrder_, 0, 1024);
                JsonReadString(*engine, "tab", engineSoundEditorPendingTab_);
                engineSoundEditorActiveTab_ = engineSoundEditorPendingTab_;

                std::string path;
                JsonReadString(*engine, "path", path);
                if (RememberedAssetExists(path)) {
                    inspectedEngineSoundPath_ = NormalizeSlashes(path);
                    inspectedEngineSoundLoaded_ = false;
                    inspectedEngineSoundError_.clear();
                    engineSoundProfileDirty_ = true;
                    bool open = false;
                    JsonReadBool(*engine, "open", open);
                    showEngineSoundEditor_ = open;
                }
            }

            if (const json::Object* vehicle = JsonSection(obj, "vehicleSoundEditor")) {
                std::string path;
                JsonReadString(*vehicle, "path", path);
                if (RememberedAssetExists(path)) {
                    inspectedVehicleSoundPath_ = NormalizeSlashes(path);
                    inspectedVehicleSoundLoaded_ = false;
                    inspectedVehicleSoundError_.clear();
                    bool open = false;
                    JsonReadBool(*vehicle, "open", open);
                    showVehicleSoundEditor_ = open;
                }
            }
        }
    }

    // Seed the comparison snapshot so a session that changes nothing never
    // rewrites the file.
    audioEditorPanelStateLastWritten_ = SerializeAudioEditorPanelState();
}

void SceneEditor::SaveAudioEditorPanelState() {
    const std::string content = SerializeAudioEditorPanelState();
    if (content == audioEditorPanelStateLastWritten_) {
        return;
    }
    std::error_code ec;
    fs::create_directories("config", ec);
    std::ofstream file(kAudioEditorPanelStatePath, std::ios::trunc);
    if (!file.is_open()) {
        return;
    }
    file << content;
    audioEditorPanelStateLastWritten_ = content;
}

void SceneEditor::TickAudioEditorPanelStatePersistence() {
    // Dragging the RPM slider changes the state every frame, so the write is
    // rate-limited rather than driven off the change itself.
    const double now = ImGui::GetTime();
    if (now < audioEditorPanelStateNextWriteTime_) {
        return;
    }
    audioEditorPanelStateNextWriteTime_ = now + 1.0;
    SaveAudioEditorPanelState();
}

void SceneEditor::PushVehicleConfigUndoState() {
    if (!showVehicleConfigEditor_ || inspectedVehicleConfigPath_.empty() || !inspectedVehicleConfigLoaded_) {
        return;
    }

    vehicleConfigUndoStack_.push_back({inspectedVehicleConfig_});
    vehicleConfigRedoStack_.clear();
    constexpr std::size_t maxHistory = 128;
    if (vehicleConfigUndoStack_.size() > maxHistory) {
        vehicleConfigUndoStack_.erase(vehicleConfigUndoStack_.begin());
    }
}

void SceneEditor::PushVehicleSoundUndoState() {
    if (!showVehicleSoundEditor_ || inspectedVehicleSoundPath_.empty() || !inspectedVehicleSoundLoaded_) {
        return;
    }

    vehicleSoundUndoStack_.push_back({inspectedVehicleSound_});
    vehicleSoundRedoStack_.clear();
    constexpr std::size_t maxHistory = 128;
    if (vehicleSoundUndoStack_.size() > maxHistory) {
        vehicleSoundUndoStack_.erase(vehicleSoundUndoStack_.begin());
    }
}

void SceneEditor::UndoVehicleConfig() {
    if (vehicleConfigUndoStack_.empty() || !showVehicleConfigEditor_) {
        return;
    }

    vehicleConfigRedoStack_.push_back({inspectedVehicleConfig_});
    inspectedVehicleConfig_ = vehicleConfigUndoStack_.back().config;
    vehicleConfigUndoStack_.pop_back();
    vehicleConfigEditActive_ = false;
}

void SceneEditor::RedoVehicleConfig() {
    if (vehicleConfigRedoStack_.empty() || !showVehicleConfigEditor_) {
        return;
    }

    vehicleConfigUndoStack_.push_back({inspectedVehicleConfig_});
    inspectedVehicleConfig_ = vehicleConfigRedoStack_.back().config;
    vehicleConfigRedoStack_.pop_back();
    vehicleConfigEditActive_ = false;
}

void SceneEditor::UndoVehicleSound() {
    if (vehicleSoundUndoStack_.empty() || !showVehicleSoundEditor_) {
        return;
    }

    vehicleSoundRedoStack_.push_back({inspectedVehicleSound_});
    inspectedVehicleSound_ = vehicleSoundUndoStack_.back().profile;
    vehicleSoundUndoStack_.pop_back();
    vehicleSoundEditActive_ = false;
}

void SceneEditor::RedoVehicleSound() {
    if (vehicleSoundRedoStack_.empty() || !showVehicleSoundEditor_) {
        return;
    }

    vehicleSoundUndoStack_.push_back({inspectedVehicleSound_});
    inspectedVehicleSound_ = vehicleSoundRedoStack_.back().profile;
    vehicleSoundRedoStack_.pop_back();
    vehicleSoundEditActive_ = false;
}

bool SceneEditor::CreateVehicleConfigAsset(const std::string& requestedName, std::string* outConfigPath) {
    std::string baseName = TrimCopyLocal(requestedName);
    if (baseName.empty()) {
        if (console_) {
            console_->AddError("Vehicle profile name cannot be empty.");
        }
        return false;
    }

    const std::string suffix = ".vehicle.json";
    const std::string lowerBaseName = ToLowerCopy(baseName);
    if (EndsWith(lowerBaseName, suffix)) {
        baseName.resize(baseName.size() - suffix.size());
    }

    std::string sanitized;
    sanitized.reserve(baseName.size());
    for (char& ch : baseName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            sanitized.push_back(ch == ' ' ? '_' : ch);
        }
    }
    sanitized = TrimCopyLocal(sanitized);
    if (sanitized.empty()) {
        if (console_) {
            console_->AddError("Vehicle profile name must contain letters or numbers.");
        }
        return false;
    }

    const fs::path assetsRoot = FindAssetsRoot();
    fs::path targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + suffix);
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) {
            console_->AddError("Vehicle profile creation blocked outside assets: " + sanitized);
        }
        return false;
    }

    int duplicateIndex = 1;
    while (fs::exists(targetPath)) {
        targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + "_" + std::to_string(duplicateIndex) + suffix);
        ++duplicateIndex;
    }

    physics::VehicleConfig config;
    config.name = sanitized;
    config.wheels = {
        {"Front Left",  {-0.85f,  1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.55f, 0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3000.0f, true,  true},
        {"Front Right", { 0.85f,  1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.55f, 0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3000.0f, true,  true},
        {"Rear Left",   {-0.85f, -1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3200.0f, true,  true},
        {"Rear Right",  { 0.85f, -1.35f, 0.0f}, 0.35f, 0.24f, 15.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 10000.0f, 8000.0f, 3200.0f, true,  true}
    };

    std::string error;
    fs::create_directories(targetPath.parent_path());
    if (!physics::VehicleConfigLoader::saveToFile(targetPath.string(), config, &error)) {
        if (console_) {
            console_->AddError(error.empty() ? ("Failed to create vehicle profile: " + sanitized) : error);
        }
        return false;
    }

    const std::string createdProjectPath = ToProjectAssetPath(targetPath, assetsRoot);
    if (outConfigPath) {
        *outConfigPath = createdProjectPath;
    }
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Created vehicle profile: " + createdProjectPath);
    }
    LogVehicleConfigValidationIssues(console_, createdProjectPath, config);
    return true;
}

bool SceneEditor::CreateVehicleSoundAsset(const std::string& requestedName, std::string* outProfilePath,
                                          const std::string& directoryOverride) {
    std::string baseName = TrimCopyLocal(requestedName);
    if (baseName.empty()) {
        if (console_) {
            console_->AddError("Vehicle sound profile name cannot be empty.");
        }
        return false;
    }

    const std::string suffix = ".vehiclesound.json";
    const std::string lowerBaseName = ToLowerCopy(baseName);
    if (EndsWith(lowerBaseName, suffix)) {
        baseName.resize(baseName.size() - suffix.size());
    }

    std::string sanitized;
    sanitized.reserve(baseName.size());
    for (char& ch : baseName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            sanitized.push_back(ch == ' ' ? '_' : ch);
        }
    }
    sanitized = TrimCopyLocal(sanitized);
    if (sanitized.empty()) {
        if (console_) {
            console_->AddError("Vehicle sound profile name must contain letters or numbers.");
        }
        return false;
    }

    const fs::path assetsRoot = FindAssetsRoot();
    const std::string targetDirectory = directoryOverride.empty() ? selectedProjectDirectory_
                                                                  : NormalizeSlashes(directoryOverride);
    fs::path targetPath = ProjectAssetPathToAbsolute(targetDirectory + "/" + sanitized + suffix);
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) {
            console_->AddError("Vehicle sound profile creation blocked outside assets: " + sanitized);
        }
        return false;
    }

    int duplicateIndex = 1;
    while (fs::exists(targetPath)) {
        targetPath = ProjectAssetPathToAbsolute(targetDirectory + "/" + sanitized + "_" + std::to_string(duplicateIndex) + suffix);
        ++duplicateIndex;
    }

    raceman::VehicleSoundProfile profile = raceman::VehicleSoundProfileLoader::makeDefault();
    profile.name = sanitized;

    std::string error;
    fs::create_directories(targetPath.parent_path());
    if (!raceman::VehicleSoundProfileLoader::saveToFile(targetPath.string(), profile, &error)) {
        if (console_) {
            console_->AddError(error.empty() ? ("Failed to create vehicle sound profile: " + sanitized) : error);
        }
        return false;
    }

    const std::string createdProjectPath = ToProjectAssetPath(targetPath, assetsRoot);
    if (outProfilePath) {
        *outProfilePath = createdProjectPath;
    }
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Created vehicle sound profile: " + createdProjectPath);
    }
    return true;
}

void SceneEditor::OpenVehicleConfigEditor(const std::string& configPath) {
    if (configPath.empty()) {
        return;
    }

    const std::string normalizedPath = NormalizeSlashes(configPath);
    const bool pathChanged = inspectedVehicleConfigPath_ != normalizedPath;
    inspectedVehicleConfigPath_ = normalizedPath;
    selectedProjectFile_ = inspectedVehicleConfigPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedVehicleConfigPath_);
    if (pathChanged) {
        inspectedVehicleConfigLoaded_ = false;
        inspectedVehicleConfigError_.clear();
        vehicleConfigUndoStack_.clear();
        vehicleConfigRedoStack_.clear();
        vehicleConfigEditActive_ = false;
    }
    showVehicleConfigEditor_ = true;
    vehicleConfigEditorFocusRequested_ = true;
    vehicleConfigEditorHighlightUntil_ = ImGui::GetTime() + 1.15;
}

void SceneEditor::PushEngineSoundUndoState() {
    if (!showEngineSoundEditor_ || inspectedEngineSoundPath_.empty() || !inspectedEngineSoundLoaded_) {
        return;
    }
    engineSoundUndoStack_.push_back({inspectedEngineSound_});
    engineSoundRedoStack_.clear();
    constexpr std::size_t maxHistory = 128;
    if (engineSoundUndoStack_.size() > maxHistory) {
        engineSoundUndoStack_.erase(engineSoundUndoStack_.begin());
    }
    engineSoundProfileDirty_ = true;
}

void SceneEditor::UndoEngineSound() {
    if (engineSoundUndoStack_.empty() || !showEngineSoundEditor_) {
        return;
    }
    engineSoundRedoStack_.push_back({inspectedEngineSound_});
    inspectedEngineSound_ = engineSoundUndoStack_.back().profile;
    engineSoundUndoStack_.pop_back();
    engineSoundEditActive_ = false;
    engineSoundProfileDirty_ = true;
}

void SceneEditor::RedoEngineSound() {
    if (engineSoundRedoStack_.empty() || !showEngineSoundEditor_) {
        return;
    }
    engineSoundUndoStack_.push_back({inspectedEngineSound_});
    inspectedEngineSound_ = engineSoundRedoStack_.back().profile;
    engineSoundRedoStack_.pop_back();
    engineSoundEditActive_ = false;
    engineSoundProfileDirty_ = true;
}

bool SceneEditor::CreateEngineSoundAsset(const std::string& requestedName, std::string* outProfilePath,
                                         const std::string& directoryOverride) {
    std::string baseName = TrimCopyLocal(requestedName);
    if (baseName.empty()) {
        if (console_) console_->AddError("Engine sound profile name cannot be empty.");
        return false;
    }

    const std::string suffix = ".enginesound.json";
    const std::string lowerBaseName = ToLowerCopy(baseName);
    if (EndsWith(lowerBaseName, suffix)) {
        baseName.resize(baseName.size() - suffix.size());
    }

    std::string sanitized;
    sanitized.reserve(baseName.size());
    for (char& ch : baseName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '-' || ch == ' ') {
            sanitized.push_back(ch == ' ' ? '_' : ch);
        }
    }
    sanitized = TrimCopyLocal(sanitized);
    if (sanitized.empty()) {
        if (console_) console_->AddError("Engine sound profile name must contain letters or numbers.");
        return false;
    }

    const fs::path assetsRoot = FindAssetsRoot();
    const std::string targetDirectory = directoryOverride.empty() ? selectedProjectDirectory_
                                                                  : NormalizeSlashes(directoryOverride);
    fs::path targetPath = ProjectAssetPathToAbsolute(targetDirectory + "/" + sanitized + suffix);
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) console_->AddError("Engine sound profile creation blocked outside assets: " + sanitized);
        return false;
    }

    int duplicateIndex = 1;
    while (fs::exists(targetPath)) {
        targetPath = ProjectAssetPathToAbsolute(targetDirectory + "/" + sanitized + "_" + std::to_string(duplicateIndex) + suffix);
        ++duplicateIndex;
    }

    raceman::EngineSoundProfile profile = raceman::EngineSoundProfileLoader::makeDefault();
    profile.name = sanitized;

    std::string error;
    fs::create_directories(targetPath.parent_path());
    if (!raceman::EngineSoundProfileLoader::saveToFile(targetPath.string(), profile, &error)) {
        if (console_) {
            console_->AddError(error.empty() ? ("Failed to create engine sound profile: " + sanitized) : error);
        }
        return false;
    }

    const std::string createdProjectPath = ToProjectAssetPath(targetPath, assetsRoot);
    if (outProfilePath) {
        *outProfilePath = createdProjectPath;
    }
    RefreshProjectFiles();
    if (console_) console_->AddLog("Created engine sound profile: " + createdProjectPath);
    return true;
}

void SceneEditor::OpenEngineSoundEditor(const std::string& profilePath) {
    if (profilePath.empty()) {
        return;
    }
    const std::string normalizedPath = NormalizeSlashes(profilePath);
    const bool pathChanged = inspectedEngineSoundPath_ != normalizedPath;
    inspectedEngineSoundPath_ = normalizedPath;
    selectedProjectFile_ = inspectedEngineSoundPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedEngineSoundPath_);
    if (pathChanged) {
        inspectedEngineSoundLoaded_ = false;
        inspectedEngineSoundError_.clear();
        engineSoundUndoStack_.clear();
        engineSoundRedoStack_.clear();
        engineSoundEditActive_ = false;
        engineSoundProfileDirty_ = true;
    }
    showEngineSoundEditor_ = true;
    engineSoundEditorFocusRequested_ = true;
    engineSoundEditorHighlightUntil_ = ImGui::GetTime() + 1.15;
}

void SceneEditor::StopEngineSoundAudition() {
    if (audioManager_ != nullptr && engineSoundAuditionVoice_ != nullptr) {
        audioManager_->StopVoice(engineSoundAuditionVoice_);
    }
    engineSoundAuditionVoice_ = nullptr;
    engineSoundAuditionSynth_.reset();
    engineSoundAuditionSweep_ = false;
}

void SceneEditor::OpenVehicleSoundEditor(const std::string& profilePath) {
    if (profilePath.empty()) {
        return;
    }

    const std::string normalizedPath = NormalizeSlashes(profilePath);
    const bool pathChanged = inspectedVehicleSoundPath_ != normalizedPath;
    inspectedVehicleSoundPath_ = normalizedPath;
    selectedProjectFile_ = inspectedVehicleSoundPath_;
    selectedProjectDirectory_ = ParentProjectDirectory(inspectedVehicleSoundPath_);
    if (pathChanged) {
        inspectedVehicleSoundLoaded_ = false;
        inspectedVehicleSoundError_.clear();
        vehicleSoundUndoStack_.clear();
        vehicleSoundRedoStack_.clear();
        vehicleSoundEditActive_ = false;
    }
    showVehicleSoundEditor_ = true;
    vehicleSoundEditorFocusRequested_ = true;
    vehicleSoundEditorHighlightUntil_ = ImGui::GetTime() + 1.15;
}

} // namespace raceman
