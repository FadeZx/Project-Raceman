#include "SceneEditorInternal.h"
#include "SceneEditorVehicleValidation.h"
#include "../audio/VehicleSoundProfile.h"
#include "../physics/VehicleConfig.h"

#include <imgui/imgui.h>
#include <cctype>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

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

bool SceneEditor::CreateVehicleSoundAsset(const std::string& requestedName, std::string* outProfilePath) {
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
    fs::path targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + suffix);
    if (!IsUnderPath(targetPath, assetsRoot)) {
        if (console_) {
            console_->AddError("Vehicle sound profile creation blocked outside assets: " + sanitized);
        }
        return false;
    }

    int duplicateIndex = 1;
    while (fs::exists(targetPath)) {
        targetPath = ProjectAssetPathToAbsolute(selectedProjectDirectory_ + "/" + sanitized + "_" + std::to_string(duplicateIndex) + suffix);
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
