#include "SceneEditorInternal.h"
#include "SceneEditorVehicleBuilder.h"

#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;
void SceneEditor::RebuildVehicleRuntime() {
    runtimeVehicles_.clear();
    int candidateCount = 0;

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        const SceneObject& object = objects_[objectIndex];
        if (!object.hasVehicle || !object.vehicle.enabled || !IsObjectEffectivelyEnabled(objectIndex)) {
            continue;
        }
        ++candidateCount;

        try {
            raceman::physics::VehicleConfig config = BuildDefaultJoltVehicleConfig();
            if (!object.vehicle.configPath.empty()) {
                try {
                    const fs::path configPath = ProjectAssetPathToAbsolute(object.vehicle.configPath);
                    config = raceman::physics::VehicleConfigLoader::loadFromFile(configPath.string());
                } catch (const std::exception& ex) {
                    if (console_) {
                        console_->AddWarning("Vehicle config load failed for '" + object.name + "', using default Jolt vehicle: " + ex.what());
                    }
                    std::fprintf(stdout,
                                 "[VehicleDebug] Config load failed for '%s' path='%s'; using default Jolt vehicle: %s\n",
                                 object.name.c_str(),
                                 object.vehicle.configPath.c_str(),
                                 ex.what());
                    std::fflush(stdout);
                }
            }
            EnsureDrivableVehicleConfig(config);
            config.transmission.mode = raceman::physics::TransmissionConfig::Mode::Automatic;
            RuntimeVehicleInstance runtimeVehicle;
            runtimeVehicle.objectId = object.id;
            runtimeVehicle.objectIndex = objectIndex;
            runtimeVehicle.chassisBodyObjectId = MakeVehicleChassisBodyObjectId(object.id);
            runtimeVehicle.manualGear = 0;
            runtimeVehicle.arcadeChassisWorld = TransformFromMatrix(GetObjectWorldMatrix(objectIndex));
            runtimeVehicle.arcadePreviousChassisWorld = runtimeVehicle.arcadeChassisWorld;
            runtimeVehicle.arcadePreviousWheelSpin = runtimeVehicle.arcadeWheelSpin;
            runtimeVehicle.arcadeInitialized = true;
            runtimeVehicle.arcadeWheelContacts.resize(config.wheels.size());

            const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(objectIndex);
            RebindRuntimeVehicleWheels(
                runtimeVehicle,
                object,
                config,
                vehicleWorldMatrix,
                [this](const std::string& id) { return FindObjectIndexById(id); },
                [this](int index) { return GetObjectWorldMatrix(index); });
            runtimeVehicle.config = config;

            runtimeVehicles_.push_back(std::move(runtimeVehicle));
            std::fprintf(stdout,
                         "[Vehicle] Loaded '%s' config='%s' with %zu wheels, chassisBody=%s\n",
                         object.name.c_str(),
                         object.vehicle.configPath.empty() ? "<default>" : object.vehicle.configPath.c_str(),
                         config.wheels.size(),
                         runtimeVehicles_.back().chassisBodyObjectId.empty() ? "<none>" : runtimeVehicles_.back().chassisBodyObjectId.c_str());
            std::fflush(stdout);
        } catch (const std::exception& ex) {
            if (console_) {
                console_->AddWarning("Vehicle runtime load failed for '" + object.name + "': " + ex.what());
            }
            std::fprintf(stdout, "[Vehicle] Runtime load failed for '%s': %s\n", object.name.c_str(), ex.what());
            std::fflush(stdout);
        }
    }

    std::fprintf(stdout, "[Vehicle] Runtime vehicles: %zu/%d\n", runtimeVehicles_.size(), candidateCount);
    std::fflush(stdout);
}

int SceneEditor::HotReloadRuntimeVehiclesForConfig(const std::string& configPath, const physics::VehicleConfig& config) {
    if (!scriptsRunning_ || runtimeVehicles_.empty()) {
        return 0;
    }

    const std::string normalizedConfigPath = NormalizeSlashes(configPath);
    int reloadedCount = 0;
    for (RuntimeVehicleInstance& runtimeVehicle : runtimeVehicles_) {
        if (runtimeVehicle.objectIndex < 0 || runtimeVehicle.objectIndex >= static_cast<int>(objects_.size())) {
            continue;
        }

        const SceneObject& object = objects_[runtimeVehicle.objectIndex];
        if (!object.hasVehicle || NormalizeSlashes(object.vehicle.configPath) != normalizedConfigPath) {
            continue;
        }

        physics::VehicleConfig runtimeConfig = config;
        EnsureDrivableVehicleConfig(runtimeConfig);
        runtimeConfig.transmission.mode = physics::TransmissionConfig::Mode::Automatic;

        const glm::mat4 vehicleWorldMatrix = GetObjectWorldMatrix(runtimeVehicle.objectIndex);
        RebindRuntimeVehicleWheels(
            runtimeVehicle,
            object,
            runtimeConfig,
            vehicleWorldMatrix,
            [this](const std::string& id) { return FindObjectIndexById(id); },
            [this](int index) { return GetObjectWorldMatrix(index); });

        runtimeVehicle.config = std::move(runtimeConfig);
        runtimeVehicle.arcadeWheelContacts.resize(runtimeVehicle.config.wheels.size());
        runtimeVehicle.arcadeGear = (std::clamp)(
            runtimeVehicle.arcadeGear,
            -1,
            (std::max)(1, static_cast<int>(runtimeVehicle.config.transmission.gearRatios.size())));
        ++reloadedCount;
    }

    if (reloadedCount > 0) {
        std::fprintf(stdout,
                     "[Vehicle] Hot-reloaded config '%s' for %d runtime vehicle(s).\n",
                     normalizedConfigPath.c_str(),
                     reloadedCount);
        std::fflush(stdout);
    }
    return reloadedCount;
}


} // namespace raceman