#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../audio/VehicleSoundProfile.h"
#include "../audio/EngineSoundProfile.h"
#include "../audio/EngineSynth.h"

#include <cmath>

namespace raceman {
using namespace scene_editor_internal;

namespace {
float LerpAudio(float a, float b, float t) { return a + (b - a) * t; }
float ClampAudio01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}

void SceneEditor::ClearVehicleSoundRuntime() {
    if (audioManager_) {
        for (auto& inst : runtimeVehicleSounds_) {
            for (auto& layer : inst.layers) {
                audioManager_->StopVoice(layer.voice);
            }
            audioManager_->StopVoice(inst.synthVoice);
            inst.synth.reset();
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
                audioManager_->PlayOneShot2D(p, trig.volume);
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
        const bool isSynthProfile = IsEngineSoundAssetPath(obj.vehicleSound.profilePath);

        RuntimeVehicleSoundInstance inst;
        inst.objectId        = obj.id;
        inst.vehicleObjectId = obj.id;
        inst.lastGear        = 0;
        inst.lastThrottleHigh = false;
        inst.lastLateralSpeed = 0.0f;
        inst.usesSynth       = isSynthProfile;

        const glm::vec3 pos = GetObjectWorldPosition(i);

        // ---- procedural path -------------------------------------------------
        if (isSynthProfile) {
            inst.engineProfile = EngineSoundProfileLoader::loadFromFile(absPath);
            // Triggers still come from the shared list, so gear-shift and tyre
            // one-shots keep working alongside the synth.
            inst.profile.triggerSounds = inst.engineProfile.triggerSounds;
            inst.profile.spatialBlend  = inst.engineProfile.spatialBlend;
            inst.profile.minDistance   = inst.engineProfile.minDistance;
            inst.profile.maxDistance   = inst.engineProfile.maxDistance;
            inst.profile.name          = inst.engineProfile.name;

            const physics::VehicleArcadeHandlingConfig* handling = nullptr;
            for (const auto& v : runtimeVehicles_) {
                if (v.objectId == obj.id) { handling = &v.config.arcadeHandling; break; }
            }
            const float idleRpm    = handling ? (std::max)(0.0f, handling->idleRPM) : 900.0f;
            const float redlineRpm = handling ? (std::max)(idleRpm + 1.0f, handling->redlineRPM) : 7000.0f;

            inst.synth = std::make_shared<EngineSynth>();
            inst.synth->SetProfile(EngineSynthBaked::Bake(inst.engineProfile, idleRpm, redlineRpm,
                                                          kEngineSynthSampleRate));
            inst.synthVoice = (inst.engineProfile.spatialBlend > 0.0f)
                ? audioManager_->CreateSynthVoice3D(inst.synth, pos)
                : audioManager_->CreateSynthVoice2D(inst.synth);

            if (inst.synthVoice != nullptr) {
                audioManager_->SetVoiceAttenuation(inst.synthVoice, inst.engineProfile.minDistance,
                                                   inst.engineProfile.maxDistance,
                                                   inst.engineProfile.spatialBlend);
                if (console_ != nullptr) {
                    char summary[256];
                    std::snprintf(summary, sizeof(summary),
                                  "Engine synth '%s': %d cylinders, rev range %.0f-%.0f rpm, minDistance=%.1f",
                                  inst.engineProfile.name.c_str(),
                                  static_cast<int>(inst.engineProfile.cylinders.size()),
                                  idleRpm, redlineRpm, inst.engineProfile.minDistance);
                    console_->AddLog(summary);
                }
            } else if (console_ != nullptr) {
                console_->AddError("Failed to create engine synth voice for " + obj.name);
                inst.synth.reset();
            }

            runtimeVehicleSounds_.push_back(std::move(inst));
            continue;
        }

        // ---- legacy sample path ----------------------------------------------
        VehicleSoundProfile profile = VehicleSoundProfileLoader::loadFromFile(absPath);
        inst.profile = profile;

        // Start all engine layers looping but paused/silent.
        for (const auto& layer : profile.engineLayers) {
            RuntimeVehicleSoundLayerState ls;
            ls.smoothVolume = 0.0f;
            ls.smoothPitch  = layer.pitchAtRpmMin;
            if (!layer.clipPath.empty()) {
                const std::string lPath = ProjectAssetPathToAbsolute(layer.clipPath).string();
                ls.voice = (profile.spatialBlend > 0.0f)
                    ? audioManager_->Play3D(lPath, pos, /*loop=*/true)
                    : audioManager_->Play2D(lPath, /*loop=*/true);
                if (ls.voice) {
                    audioManager_->SetVoiceVolume(ls.voice, 0.0f);
                    audioManager_->SetVoicePitch(ls.voice, ls.smoothPitch);
                    audioManager_->SetVoiceAttenuation(ls.voice, profile.minDistance,
                                                       profile.maxDistance, profile.spatialBlend);
                } else if (console_ != nullptr) {
                    // Previously silent: a layer whose clip is missing just left a
                    // null handle, so a profile could lose most of its engine sound
                    // with nothing reported anywhere.
                    console_->AddError("Vehicle sound layer failed to load: " + layer.clipPath
                                       + "  (resolved to " + lPath + ")");
                }
            }
            inst.layers.push_back(std::move(ls));
        }

        if (console_ != nullptr) {
            int loaded = 0;
            for (const auto& ls : inst.layers) {
                if (ls.voice != nullptr) ++loaded;
            }
            char summary[256];
            std::snprintf(summary, sizeof(summary),
                          "Vehicle sound '%s': %d/%d engine layers loaded, minDistance=%.2f maxDistance=%.1f",
                          profile.name.c_str(), loaded,
                          static_cast<int>(inst.layers.size()),
                          profile.minDistance, profile.maxDistance);
            console_->AddLog(summary);
            if (loaded > 0 && profile.minDistance < 2.0f) {
                // Inverse attenuation falls off as minDistance/distance, so a
                // sub-2m minDistance puts a chase camera tens of dB down and the
                // car reads as silent however loud the clip is.
                console_->AddWarning("Vehicle sound minDistance is very small; the engine will be "
                                     "near-inaudible from a chase camera. Try 5-15 m.");
            }
        }
        runtimeVehicleSounds_.push_back(std::move(inst));
    }

    // Play EngineStart triggers
    for (auto& inst : runtimeVehicleSounds_) {
        for (const auto& trig : inst.profile.triggerSounds) {
            if (trig.trigger == VehicleSoundTrigger::EngineStart && !trig.clipPath.empty()) {
                const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                audioManager_->PlayOneShot2D(p, trig.volume);
            }
        }
    }
}

void SceneEditor::ApplyEngineSoundEditsToRuntime() {
    if (!showEngineSoundEditor_ || !inspectedEngineSoundLoaded_ || inspectedEngineSoundPath_.empty()) {
        return;
    }
    const std::string edited = NormalizeSlashes(inspectedEngineSoundPath_);

    for (auto& inst : runtimeVehicleSounds_) {
        if (!inst.usesSynth || !inst.synth) {
            continue;
        }
        const int idx = FindObjectIndexById(inst.objectId);
        if (idx < 0 || NormalizeSlashes(objects_[idx].vehicleSound.profilePath) != edited) {
            continue;
        }

        const physics::VehicleArcadeHandlingConfig* handling = nullptr;
        for (const auto& v : runtimeVehicles_) {
            if (v.objectId == inst.objectId) { handling = &v.config.arcadeHandling; break; }
        }
        const float idleRpm    = handling ? (std::max)(0.0f, handling->idleRPM) : 900.0f;
        const float redlineRpm = handling ? (std::max)(idleRpm + 1.0f, handling->redlineRPM) : 7000.0f;

        inst.engineProfile = inspectedEngineSound_;
        // Bake happens here on the game thread; the audio thread only ever swaps
        // an immutable shared_ptr, so this is safe mid-playback.
        inst.synth->SetProfile(EngineSynthBaked::Bake(inst.engineProfile, idleRpm, redlineRpm,
                                                      kEngineSynthSampleRate));
        if (inst.synthVoice != nullptr) {
            audioManager_->SetVoiceAttenuation(inst.synthVoice, inst.engineProfile.minDistance,
                                               inst.engineProfile.maxDistance,
                                               inst.engineProfile.spatialBlend);
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
        // Non-const: the engine state's shift-event queue is drained here.
        RuntimeVehicleInstance* rv = nullptr;
        for (auto& v : runtimeVehicles_) {
            if (v.objectId == inst.vehicleObjectId) { rv = &v; break; }
        }
        if (!rv) continue;

        // The inertial engine model is the truth for audio; the kinematic
        // arcadeEngineRPM has no inertia and cannot rev in neutral.
        const physics::VehicleEngineState& engine = rv->engineState;
        const float rpm      = engine.rpm;
        const float throttle = engine.throttle;
        const float latSpd   = std::abs(rv->arcadeLateralSpeed);

        // 3D position — follow the vehicle
        const int vIdx = FindObjectIndexById(inst.vehicleObjectId);
        const glm::vec3 vehiclePos = (vIdx >= 0) ? GetObjectWorldPosition(vIdx) : glm::vec3(0.0f);
        if (vIdx >= 0 && inst.profile.spatialBlend > 0.0f) {
            for (auto& ls : inst.layers) {
                if (ls.voice) audioManager_->SetVoicePosition(ls.voice, vehiclePos);
            }
            if (inst.synthVoice) audioManager_->SetVoicePosition(inst.synthVoice, vehiclePos);
        }

        // ---- procedural engine ------------------------------------------------
        if (inst.usesSynth && inst.synth) {
            const physics::VehicleArcadeHandlingConfig& handling = rv->config.arcadeHandling;

            EngineSynthParams params;
            params.rpm         = rpm;
            params.idleRpm     = (std::max)(0.0f, handling.idleRPM);
            params.redlineRpm  = (std::max)(params.idleRpm + 1.0f, handling.redlineRPM);
            params.load        = engine.load;
            params.throttle    = engine.throttle;
            params.boost       = engine.boost;
            params.ignitionCut = engine.shiftCut || engine.limiterCut;
            params.overrun     = engine.throttle < 0.05f && rpm > inst.engineProfile.overrun.minRpm;
            params.volume      = 1.0f;
            inst.synth->SetParams(params);

            // Overrun crackle. Injected into the exhaust waveguide so it exits
            // through the muffler and tailpipe like any other pulse.
            inst.overrunPopCooldown = (std::max)(0.0f, inst.overrunPopCooldown - deltaTime);
            if (inst.engineProfile.overrun.enabled && params.overrun && inst.overrunPopCooldown <= 0.0f) {
                const float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
                if (roll < inst.engineProfile.overrun.density) {
                    EngineSynthEvent event;
                    event.kind = EngineSynthEventKind::Backfire;
                    event.strength = 0.5f + 0.5f * roll;
                    inst.synth->PushEvent(event);
                }
                inst.overrunPopCooldown = 0.04f;
            }

            // Limiter pops on each fuel cut edge.
            if (engine.limiterCut && !inst.lastLimiterCut) {
                EngineSynthEvent event;
                event.kind = EngineSynthEventKind::LimiterPop;
                event.strength = inst.engineProfile.overrun.limiterPopGain;
                inst.synth->PushEvent(event);
            }
            inst.lastLimiterCut = engine.limiterCut;
        }

        // Update each engine layer (legacy sample path only)
        for (std::size_t li = 0; !inst.usesSynth && li < inst.layers.size()
                                 && li < inst.profile.engineLayers.size(); ++li) {
            const VehicleSoundEngineLayer& def = inst.profile.engineLayers[li];
            RuntimeVehicleSoundLayerState& ls  = inst.layers[li];
            if (!ls.voice) continue;

            const float range = def.rpmMax - def.rpmMin;
            const float t     = (range > 0.0f) ? ClampAudio01((rpm - def.rpmMin) / range) : 0.0f;

            const float targetPitch  = LerpAudio(def.pitchAtRpmMin, def.pitchAtRpmMax, t);
            float       targetVolume = LerpAudio(def.volumeAtRpmMin, def.volumeAtRpmMax, t);
            targetVolume += throttle * def.volumeThrottleScale;
            targetVolume  = ClampAudio01(targetVolume) * inst.profile.masterVolume;

            ls.smoothPitch  = LerpAudio(ls.smoothPitch,  targetPitch,  smoothRate);
            ls.smoothVolume = LerpAudio(ls.smoothVolume, targetVolume, smoothRate);

            audioManager_->SetVoicePitch(ls.voice, ls.smoothPitch);
            audioManager_->SetVoiceVolume(ls.voice, ls.smoothVolume);
        }

        // ---- shift triggers, drained from the fixed-step event queue ----------
        // Polling arcadeGear here used to collapse two shifts in one rendered
        // frame into one trigger, and the guard against gear 0 meant the launch
        // shift never fired at all. The queue has neither problem.
        for (int e = 0; e < rv->engineState.shiftEventCount; ++e) {
            const physics::EngineShiftEvent& shift = rv->engineState.shiftEvents[e];
            VehicleSoundTrigger want = VehicleSoundTrigger::GearUp;
            switch (shift.kind) {
                case physics::EngineShiftKind::Up:
                case physics::EngineShiftKind::Launch:
                    want = VehicleSoundTrigger::GearUp;
                    break;
                case physics::EngineShiftKind::Down:
                    want = VehicleSoundTrigger::GearDown;
                    break;
                case physics::EngineShiftKind::ToNeutral:
                case physics::EngineShiftKind::ToReverse:
                    continue; // no clip for these
            }
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == want && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    audioManager_->PlayOneShot2D(p, trig.volume);
                    break;
                }
            }
            if (inst.usesSynth && inst.synth) {
                EngineSynthEvent event;
                event.kind = EngineSynthEventKind::ShiftCut;
                event.strength = 1.0f;
                inst.synth->PushEvent(event);
            }
        }
        rv->engineState.ClearShiftEvents();

        const int curGear = rv->arcadeGear;
        const bool throttleHigh = throttle > 0.7f;

        // Sample-based backfire only applies to legacy profiles; the synth makes
        // its own through the exhaust.
        if (!inst.usesSynth && inst.lastThrottleHigh && !throttleHigh && rpm > 0.0f) {
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == VehicleSoundTrigger::Backfire &&
                    rpm >= trig.minRpmForBackfire && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    audioManager_->PlayOneShot2D(p, trig.volume);
                    break;
                }
            }
        }

        // Blow-off on a throttle lift with boost up.
        if (inst.usesSynth && inst.synth && inst.engineProfile.turbo.enabled &&
            inst.lastThrottleHigh && !throttleHigh && engine.boost > 0.25f) {
            EngineSynthEvent event;
            event.kind = EngineSynthEventKind::BlowOff;
            event.strength = engine.boost;
            inst.synth->PushEvent(event);
        }

        if (latSpd > 0.0f) {
            const bool squealNow  = latSpd > 2.0f;
            const bool squealPrev = inst.lastLateralSpeed > 2.0f;
            if (squealNow && !squealPrev) {
                for (const auto& trig : inst.profile.triggerSounds) {
                    if (trig.trigger == VehicleSoundTrigger::TireSqueal &&
                        latSpd >= trig.minLateralSpeedForSqueal && !trig.clipPath.empty()) {
                        const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                        audioManager_->PlayOneShot2D(p, trig.volume);
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
