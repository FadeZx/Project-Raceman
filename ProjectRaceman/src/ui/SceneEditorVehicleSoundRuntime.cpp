#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../audio/VehicleSoundProfile.h"

#include <irrKlang/irrKlang.h>

#include <cmath>

namespace raceman {
using namespace scene_editor_internal;

namespace {
float LerpAudio(float a, float b, float t) { return a + (b - a) * t; }
float ClampAudio01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}

void SceneEditor::ClearVehicleSoundRuntime() {
    for (auto& inst : runtimeVehicleSounds_) {
        for (auto& layer : inst.layers) {
            if (layer.sound) {
                layer.sound->stop();
                layer.sound->drop();
                layer.sound = nullptr;
            }
        }
    }
    runtimeVehicleSounds_.clear();
}

void SceneEditor::PlayVehicleSoundStopTriggers() {
    if (!audioManager_ || !audioManager_->IsInitialized()) {
        return;
    }

    for (auto& inst : runtimeVehicleSounds_) {
        for (const auto& trig : inst.profile.triggerSounds) {
            if (trig.trigger == VehicleSoundTrigger::EngineStop && !trig.clipPath.empty()) {
                const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                if (s) {
                    s->setVolume(trig.volume);
                    s->drop();
                }
                break;
            }
        }
    }
}

void SceneEditor::RebuildVehicleSoundRuntime() {
    if (!audioManager_ || !audioManager_->IsInitialized()) {
        return;
    }

    // -- Vehicle sound profiles --
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& obj = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !obj.hasVehicleSound || !obj.vehicleSound.enabled) continue;
        if (obj.vehicleSound.profilePath.empty()) continue;
        // Must also have a Vehicle component to get telemetry.
        if (!obj.hasVehicle || !obj.vehicle.enabled) continue;

        const std::string absPath = ProjectAssetPathToAbsolute(obj.vehicleSound.profilePath).string();
        VehicleSoundProfile profile = VehicleSoundProfileLoader::loadFromFile(absPath);

        RuntimeVehicleSoundInstance inst;
        inst.objectId        = obj.id;
        inst.vehicleObjectId = obj.id;
        inst.profile         = profile;
        inst.lastGear        = 0;
        inst.lastThrottleHigh = false;
        inst.lastLateralSpeed = 0.0f;

        // Start all engine layers looping but paused/silent.
        const glm::vec3 pos = GetObjectWorldPosition(i);
        for (const auto& layer : profile.engineLayers) {
            RuntimeVehicleSoundLayerState ls;
            ls.smoothVolume = 0.0f;
            ls.smoothPitch  = layer.pitchAtRpmMin;
            if (!layer.clipPath.empty()) {
                const std::string lPath = ProjectAssetPathToAbsolute(layer.clipPath).string();
                ls.sound = (profile.spatialBlend > 0.5f)
                    ? audioManager_->Play3D(lPath, pos, /*loop=*/true, /*paused=*/false)
                    : audioManager_->Play2D(lPath, /*loop=*/true, /*paused=*/false);
                if (ls.sound) {
                    ls.sound->setVolume(0.0f);
                    ls.sound->setPlaybackSpeed(ls.smoothPitch);
                    if (profile.spatialBlend > 0.5f) {
                        ls.sound->setMinDistance(profile.minDistance);
                    }
                }
            }
            inst.layers.push_back(std::move(ls));
        }
        runtimeVehicleSounds_.push_back(std::move(inst));
    }

    // Play EngineStart triggers
    for (auto& inst : runtimeVehicleSounds_) {
        for (const auto& trig : inst.profile.triggerSounds) {
            if (trig.trigger == VehicleSoundTrigger::EngineStart && !trig.clipPath.empty()) {
                const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                if (s) { s->setVolume(trig.volume); s->drop(); }
            }
        }
    }
}

void SceneEditor::UpdateVehicleSoundRuntime(float deltaTime) {
    if (!audioManager_ || !audioManager_->IsInitialized()) {
        return;
    }

    // -- Update vehicle sound layers --
    const float smoothRate = 8.0f * deltaTime; // slew rate

    for (auto& inst : runtimeVehicleSounds_) {
        // Find the RuntimeVehicleInstance for this vehicle.
        const RuntimeVehicleInstance* rv = nullptr;
        for (const auto& v : runtimeVehicles_) {
            if (v.objectId == inst.vehicleObjectId) { rv = &v; break; }
        }
        if (!rv) continue;

        const float rpm      = rv->arcadeEngineRPM;
        const float throttle = rv->arcadeThrottle;
        const float latSpd   = std::abs(rv->arcadeLateralSpeed);

        // 3D position — follow the vehicle
        const int vIdx = FindObjectIndexById(inst.vehicleObjectId);
        if (vIdx >= 0 && inst.profile.spatialBlend > 0.5f) {
            const glm::vec3 pos = GetObjectWorldPosition(vIdx);
            const irrklang::vec3df ipos(pos.x, pos.y, pos.z);
            for (auto& ls : inst.layers) {
                if (ls.sound) ls.sound->setPosition(ipos);
            }
        }

        // Update each engine layer
        for (std::size_t li = 0; li < inst.layers.size() && li < inst.profile.engineLayers.size(); ++li) {
            const VehicleSoundEngineLayer& def = inst.profile.engineLayers[li];
            RuntimeVehicleSoundLayerState& ls  = inst.layers[li];
            if (!ls.sound) continue;

            const float range = def.rpmMax - def.rpmMin;
            const float t     = (range > 0.0f) ? ClampAudio01((rpm - def.rpmMin) / range) : 0.0f;

            const float targetPitch  = LerpAudio(def.pitchAtRpmMin, def.pitchAtRpmMax, t);
            float       targetVolume = LerpAudio(def.volumeAtRpmMin, def.volumeAtRpmMax, t);
            targetVolume += throttle * def.volumeThrottleScale;
            targetVolume  = ClampAudio01(targetVolume) * inst.profile.masterVolume;

            ls.smoothPitch  = LerpAudio(ls.smoothPitch,  targetPitch,  smoothRate);
            ls.smoothVolume = LerpAudio(ls.smoothVolume, targetVolume, smoothRate);

            ls.sound->setPlaybackSpeed(ls.smoothPitch);
            ls.sound->setVolume(ls.smoothVolume);
        }

        // Trigger detection
        const int curGear = rv->arcadeGear;
        if (curGear != inst.lastGear && inst.lastGear != 0) {
            const bool up = curGear > inst.lastGear;
            const VehicleSoundTrigger want = up ? VehicleSoundTrigger::GearUp : VehicleSoundTrigger::GearDown;
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == want && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                    if (s) { s->setVolume(trig.volume); s->drop(); }
                    break;
                }
            }
        }

        const bool throttleHigh = throttle > 0.7f;
        if (inst.lastThrottleHigh && !throttleHigh && rpm > 0.0f) {
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == VehicleSoundTrigger::Backfire &&
                    rpm >= trig.minRpmForBackfire && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                    if (s) { s->setVolume(trig.volume); s->drop(); }
                    break;
                }
            }
        }

        if (latSpd > 0.0f) {
            const bool squealNow  = latSpd > 2.0f;
            const bool squealPrev = inst.lastLateralSpeed > 2.0f;
            if (squealNow && !squealPrev) {
                for (const auto& trig : inst.profile.triggerSounds) {
                    if (trig.trigger == VehicleSoundTrigger::TireSqueal &&
                        latSpd >= trig.minLateralSpeedForSqueal && !trig.clipPath.empty()) {
                        const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                        irrklang::ISound* s = audioManager_->Play2D(p, false, false);
                        if (s) { s->setVolume(trig.volume); s->drop(); }
                        break;
                    }
                }
            }
        }

        inst.lastGear         = curGear;
        inst.lastThrottleHigh = throttleHigh;
        inst.lastLateralSpeed = latSpd;
    }
}

} // namespace raceman
