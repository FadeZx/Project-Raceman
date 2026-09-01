#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../audio/VehicleSoundProfile.h"
#include "../audio/EngineSoundProfile.h"
#include "../audio/EngineSynth.h"
#include "../audio/TyreSoundProfile.h"
#include "../audio/TyreSynth.h"

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
            audioManager_->StopVoice(inst.synthVoice);
            inst.synth.reset();
            audioManager_->StopVoice(inst.tyreVoice);
            inst.tyreSynth.reset();
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

        RuntimeVehicleSoundInstance inst;
        inst.objectId        = obj.id;
        inst.vehicleObjectId = obj.id;
        inst.lastGear        = 0;
        inst.lastThrottleHigh = false;
        inst.lastLateralSpeed = 0.0f;

        const glm::vec3 pos = GetObjectWorldPosition(i);

        inst.engineProfile = EngineSoundProfileLoader::loadFromFile(absPath);
        inst.profile.name = inst.engineProfile.name;

        // Emission and triggers have exactly one home: the component. There is
        // no fallback to a profile copy any more, because two homes for one
        // value is how they drifted apart.
        inst.profile.triggerSounds = obj.vehicleSound.triggerSounds;
        inst.profile.spatialBlend  = obj.vehicleSound.spatialBlend;
        inst.profile.minDistance   = obj.vehicleSound.minDistance;
        inst.profile.maxDistance   = obj.vehicleSound.maxDistance;

        // The rev range comes from the vehicle, not from the sound profile.
        const physics::EngineConfig* engineConfig = nullptr;
        for (const auto& v : runtimeVehicles_) {
            if (v.objectId == obj.id) { engineConfig = &v.config.engine; break; }
        }
        const float idleRpm    = (std::max)(0.0f, engineConfig ? engineConfig->idleRPM : 900.0f);
        const float redlineRpm = (std::max)(idleRpm + 1.0f, engineConfig ? engineConfig->redlineRPM : 7000.0f);

        inst.synth = std::make_shared<EngineSynth>();
        inst.synth->SetProfile(EngineSynthBaked::Bake(inst.engineProfile, idleRpm, redlineRpm,
                                                      kEngineSynthSampleRate));
        inst.synthVoice = (obj.vehicleSound.spatialBlend > 0.0f)
            ? audioManager_->CreateSynthVoice3D(inst.synth, pos)
            : audioManager_->CreateSynthVoice2D(inst.synth);

        if (inst.synthVoice != nullptr) {
            audioManager_->SetVoiceAttenuation(inst.synthVoice, obj.vehicleSound.minDistance,
                                               obj.vehicleSound.maxDistance,
                                               obj.vehicleSound.spatialBlend);
            if (console_ != nullptr) {
                char summary[256];
                std::snprintf(summary, sizeof(summary),
                              "Engine synth '%s': %d cylinders, rev range %.0f-%.0f rpm (from vehicle), minDistance=%.1f",
                              inst.engineProfile.name.c_str(),
                              static_cast<int>(inst.engineProfile.cylinders.size()),
                              idleRpm, redlineRpm, obj.vehicleSound.minDistance);
                console_->AddLog(summary);
            }
        } else if (console_ != nullptr) {
            console_->AddError("Failed to create engine synth voice for " + obj.name);
            inst.synth.reset();
        }

        CreateVehicleTyreVoice(inst, obj, pos);
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

void SceneEditor::CreateVehicleTyreVoice(RuntimeVehicleSoundInstance& inst, const SceneObject& obj,
                                         const glm::vec3& position) {
    if (audioManager_ == nullptr || !audioManager_->IsInitialized()) {
        return;
    }
    // No profile assigned is a valid choice - the car simply has no tyre voice.
    if (obj.vehicleSound.tyreProfilePath.empty()) {
        return;
    }
    const std::string absolute = ProjectAssetPathToAbsolute(obj.vehicleSound.tyreProfilePath).string();
    inst.tyreProfile = std::make_shared<TyreSoundProfile>(
        TyreSoundProfileLoader::loadFromFile(absolute));
    inst.tyreSynth = std::make_shared<TyreSynth>();
    inst.tyreSynth->SetProfile(inst.tyreProfile);

    inst.tyreVoice = (inst.tyreProfile->spatialBlend > 0.0f)
        ? audioManager_->CreateSynthVoice3D(inst.tyreSynth, position)
        : audioManager_->CreateSynthVoice2D(inst.tyreSynth);
    if (inst.tyreVoice != nullptr) {
        audioManager_->SetVoiceAttenuation(inst.tyreVoice, inst.tyreProfile->minDistance,
                                           inst.tyreProfile->maxDistance,
                                           inst.tyreProfile->spatialBlend);
        if (console_ != nullptr) {
            console_->AddLog("Tyre sound '" + inst.tyreProfile->name + "' active.");
        }
    } else if (console_ != nullptr) {
        console_->AddError("Failed to create tyre voice for " + obj.name);
        inst.tyreSynth.reset();
    }
}

void SceneEditor::UpdateVehicleTyreSound(RuntimeVehicleSoundInstance& inst,
                                         RuntimeVehicleInstance& vehicle,
                                         const glm::vec3& vehiclePos, float listenerDistance,
                                         float deltaTime) {
    if (!inst.tyreSynth || inst.tyreVoice == nullptr || !inst.tyreProfile) {
        return;
    }
    if (audioManager_ != nullptr) {
        audioManager_->SetVoicePosition(inst.tyreVoice, vehiclePos);
    }

    TyreSynthParams params;
    params.speedMps = std::fabs(vehicle.arcadeSpeed);
    // Reference speed comes from the vehicle, not the tyre profile: "full
    // rolling noise" means at this car's top speed, so one tyre asset behaves
    // correctly on a GT3 car and on a road car without being re-authored.
    params.speedReferenceMps = (std::max)(1.0f, vehicle.config.arcadeHandling.maxForwardSpeed);

    const int wheelCount = (std::min)(static_cast<int>(vehicle.arcadeWheelContacts.size()),
                                      TyreSynthParams::kMaxWheels);
    params.wheelCount = wheelCount;

    // Normalise wheel load against the average, so "loaded" means loaded
    // relative to this car rather than to an absolute force.
    float totalForce = 0.0f;
    for (int w = 0; w < wheelCount; ++w) {
        totalForce += (std::max)(0.0f, vehicle.arcadeWheelContacts[w].normalForce);
    }
    const float averageForce = (wheelCount > 0) ? (std::max)(1.0f, totalForce / wheelCount) : 1.0f;

    // Slip is per wheel now: each tyre reports how fast its own contact patch
    // is sliding, so the outside front singing on turn-in and the inside rear
    // going quiet as it unloads are separate events in the mix. Same numbers
    // the skid decals are drawn from, so a mark and its squeal start together.

    for (int w = 0; w < wheelCount; ++w) {
        const RuntimeVehicleWheelContact& contact = vehicle.arcadeWheelContacts[static_cast<std::size_t>(w)];
        TyreWheelParams& out = params.wheels[w];

        out.grounded = contact.grounded;
        out.load = (std::clamp)(contact.normalForce / averageForce * 0.5f, 0.0f, 1.0f);

        // Raw metres per second, split by axis, plus the sim's own verdict on
        // what kind of slip this is. Handing the synth a single pre-normalised
        // 0..1 threw all three apart: a lock-up, a slide and a wheelspin came
        // out as the same squeal at the same pitch.
        out.lateralSlideMps = (std::max)(0.0f, contact.lateralSlideMps);
        out.longitudinalSlideMps = (std::max)(0.0f, contact.longitudinalSlideMps);
        out.locked = contact.locked;
        out.spinning = contact.spinning;

        const int surface = (std::clamp)(static_cast<int>(contact.surfaceType), 0,
                                         kTrackSurfaceTypeCount - 1);
        out.surfaceA = surface;
        out.surfaceB = surface;
        out.surfaceBlend = 0.0f;

        // Suspension moving fast means a kerb strike or a landing. Rate rather
        // than absolute travel, so simply sitting compressed is silent.
        out.impact = 0.0f;
        if (inst.lastSuspensionValid && deltaTime > 0.0f && contact.grounded) {
            const float rate = (contact.suspensionTravel - inst.lastSuspensionTravel[w]) / deltaTime;
            const float threshold = (std::max)(0.01f, inst.tyreProfile->impactThreshold);
            if (rate > threshold) {
                out.impact = (std::clamp)((rate - threshold) / (threshold * 4.0f), 0.0f, 1.0f);
            }
        }
        inst.lastSuspensionTravel[w] = contact.suspensionTravel;
    }
    inst.lastSuspensionValid = wheelCount > 0;

    // Same air absorption curve the engine uses, so the two stay coherent.
    const EnginePerspectiveSettings& persp = inst.engineProfile.perspective;
    const float absorption =
        (std::clamp)(listenerDistance / (std::max)(1.0f, persp.airAbsorptionMetres), 0.0f, 1.0f);
    params.lowPass = 1.0f - 0.85f * absorption;
    params.volume = 1.0f;

    inst.tyreSynth->SetParams(params);
}

void SceneEditor::ApplyEngineSoundEditsToRuntime() {
    if (!showEngineSoundEditor_ || !inspectedEngineSoundLoaded_ || inspectedEngineSoundPath_.empty()) {
        return;
    }
    const std::string edited = NormalizeSlashes(inspectedEngineSoundPath_);

    for (auto& inst : runtimeVehicleSounds_) {
        if (!inst.synth) {
            continue;
        }
        const int idx = FindObjectIndexById(inst.objectId);
        if (idx < 0 || NormalizeSlashes(objects_[idx].vehicleSound.profilePath) != edited) {
            continue;
        }

        // The rev range comes from the vehicle, not from the sound profile.
        const physics::EngineConfig* engineConfig = nullptr;
        for (const auto& v : runtimeVehicles_) {
            if (v.objectId == inst.objectId) { engineConfig = &v.config.engine; break; }
        }
        const float idleRpm    = (std::max)(0.0f, engineConfig ? engineConfig->idleRPM : 900.0f);
        const float redlineRpm = (std::max)(idleRpm + 1.0f, engineConfig ? engineConfig->redlineRPM : 7000.0f);

        inst.engineProfile = inspectedEngineSound_;
        // Bake happens here on the game thread; the audio thread only ever swaps
        // an immutable shared_ptr, so this is safe mid-playback.
        inst.synth->SetProfile(EngineSynthBaked::Bake(inst.engineProfile, idleRpm, redlineRpm,
                                                      kEngineSynthSampleRate));
        if (inst.synthVoice != nullptr) {
            audioManager_->SetVoiceAttenuation(inst.synthVoice, inst.profile.minDistance,
                                               inst.profile.maxDistance,
                                               inst.profile.spatialBlend);
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
            if (inst.synthVoice) audioManager_->SetVoicePosition(inst.synthVoice, vehiclePos);
        }

        // Doppler. The vehicle's own velocity vector comes from its forward axis
        // and signed speed; a passing car is one of the most recognisable sounds
        // there is, and it needs both ends of the equation.
        if (inst.synthVoice != nullptr && vIdx >= 0) {
            const glm::mat4 vehicleMatrix = GetObjectWorldMatrix(vIdx);
            const glm::vec3 forward =
                glm::normalize(glm::vec3(vehicleMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            const glm::vec3 velocity = forward * rv->arcadeSpeed;
            audioManager_->SetVoiceVelocity(inst.synthVoice, velocity,
                                            inst.engineProfile.perspective.dopplerFactor);
        }

        // ---- procedural engine ------------------------------------------------
        if (inst.synth) {
            const physics::VehicleArcadeHandlingConfig& handling = rv->config.arcadeHandling;

            EngineSynthParams params;
            params.rpm         = rpm;
            params.idleRpm     = (std::max)(0.0f, rv->config.engine.idleRPM);
            params.redlineRpm  = (std::max)(params.idleRpm + 1.0f, rv->config.engine.redlineRPM);
            params.load        = engine.load;
            params.throttle    = engine.throttle;
            params.boost       = engine.boost;
            params.ignitionCut = engine.shiftCut || engine.limiterCut;
            // A limiter genuinely kills fuel; a shift only interrupts it, and how
            // far is the profile's authored shiftCutDepth. That parameter existed
            // in the asset and editor but was never read, so every shift was a
            // full silence.
            params.ignitionScale = engine.limiterCut
                ? 0.0f
                : (1.0f - (std::clamp)(inst.engineProfile.drivetrain.shiftCutDepth, 0.0f, 1.0f));

            // ---- perspective, air and space ----------------------------------
            const EnginePerspectiveSettings& persp = inst.engineProfile.perspective;
            if (persp.enabled && vIdx >= 0) {
                glm::vec3 listenerPos(0.0f), listenerFwd(0.0f, 0.0f, -1.0f), listenerUp(0.0f, 1.0f, 0.0f);
                GetActiveAudioListenerPose(listenerPos, listenerFwd, listenerUp);

                const glm::vec3 toListener = listenerPos - vehiclePos;
                const float distance = glm::length(toListener);

                // Where the listener sits relative to the car. Behind the car you
                // hear the tailpipe; in front you hear the intake shouting.
                const glm::mat4 vehicleMatrix = GetObjectWorldMatrix(vIdx);
                const glm::vec3 vehicleForward =
                    glm::normalize(glm::vec3(vehicleMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                const float behind = (distance > 0.01f)
                    ? (std::clamp)(glm::dot(glm::normalize(toListener), -vehicleForward), -1.0f, 1.0f)
                    : 0.0f;
                const float rearMix = behind * 0.5f + 0.5f; // 0 = ahead, 1 = astern

                params.exhaustWeight = persp.exhaustFront + (persp.exhaustRear - persp.exhaustFront) * rearMix;
                params.intakeWeight  = persp.intakeFront  + (persp.intakeRear  - persp.intakeFront)  * rearMix;
                // Mechanical clatter is quiet and local; it disappears long
                // before the exhaust does.
                params.blockWeight = 1.0f / (1.0f + distance / (std::max)(1.0f, persp.blockFalloffMetres));

                // Air absorbs the top end with distance. Without this a car at
                // 100 m is tonally identical to one at 5 m, only quieter, which
                // is one of the clearest giveaways of synthetic audio.
                const float absorption =
                    (std::clamp)(distance / (std::max)(1.0f, persp.airAbsorptionMetres), 0.0f, 1.0f);
                params.lowPass = 1.0f - 0.85f * absorption;

                // Direct sound falls off faster than the reverberant field, so
                // the wet/dry ratio climbs with distance.
                params.reverbWet =
                    0.35f + 1.15f * (std::clamp)(distance / (std::max)(1.0f, persp.reverbDistanceMetres), 0.0f, 1.0f);

                // Up close you are effectively inside the engine bay and hear
                // each cylinder; step back and the bodywork radiates the low
                // orders instead, so the engine reads an octave bigger.
                params.octaveTilt =
                    (std::clamp)(distance / (std::max)(1.0f, persp.octaveTiltMetres), 0.0f, 1.0f)
                    * (std::clamp)(persp.octaveTiltAmount, 0.0f, 1.0f);

                // Acoustics come from where the LISTENER is, not from the car.
                // Drive into a tunnel and every car you can hear picks up the
                // tunnel, which is the whole point of doing it this way.
                const ResolvedAudioEnvironment env = ResolveListenerEnvironment(listenerPos);
                params.reverbEarlyGain        = env.earlyGain;
                params.reverbEarlySpreadMs    = env.earlySpreadMs;
                params.reverbTailGain         = env.tailGain;
                params.reverbTailDecaySeconds = env.tailDecaySeconds;
                params.reverbTailDamping      = env.tailDamping;
                params.lowPass *= env.lowPassScale;
                params.volume  *= env.volumeScale;
            }
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

            // Fuel returning after an upshift ignites what is left in the pipe:
            // that crack is the sound of a race gearbox, and it is what the long
            // silent cut was hiding.
            if (inst.lastShiftCut && !engine.shiftCut) {
                EngineSynthEvent event;
                event.kind = EngineSynthEventKind::Backfire;
                event.strength = inst.engineProfile.overrun.limiterPopGain;
                inst.synth->PushEvent(event);
            }
            inst.lastShiftCut = engine.shiftCut;

            // Limiter pops on each fuel cut edge.
            if (engine.limiterCut && !inst.lastLimiterCut) {
                EngineSynthEvent event;
                event.kind = EngineSynthEventKind::LimiterPop;
                event.strength = inst.engineProfile.overrun.limiterPopGain;
                inst.synth->PushEvent(event);
            }
            inst.lastLimiterCut = engine.limiterCut;
        }

        // Tyres. Continuous, and driven by per-wheel telemetry the sim was
        // already producing and audio had never read.
        if (inst.tyreSynth) {
            glm::vec3 lp(0.0f), lf(0.0f, 0.0f, -1.0f), lu(0.0f, 1.0f, 0.0f);
            GetActiveAudioListenerPose(lp, lf, lu);
            UpdateVehicleTyreSound(inst, *rv, vehiclePos, glm::length(lp - vehiclePos), deltaTime);
        }

        // Update each engine layer (legacy sample path only)
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
                case physics::EngineShiftKind::ToNeutral:
                case physics::EngineShiftKind::ToReverse:
                    // No dedicated clip type for these; the down-shift clunk
                    // is the closest thing authored and reads fine for any
                    // lever movement, so reuse it rather than play nothing.
                    want = VehicleSoundTrigger::GearDown;
                    break;
            }
            for (const auto& trig : inst.profile.triggerSounds) {
                if (trig.trigger == want && !trig.clipPath.empty()) {
                    const std::string p = ProjectAssetPathToAbsolute(trig.clipPath).string();
                    audioManager_->PlayOneShot2D(p, trig.volume);
                    break;
                }
            }
            if (inst.synth) {
                EngineSynthEvent event;
                event.kind = EngineSynthEventKind::ShiftCut;
                event.strength = 1.0f;
                inst.synth->PushEvent(event);
            }
        }
        rv->engineState.ClearShiftEvents();

        const int curGear = rv->arcadeGear;
        const bool throttleHigh = throttle > 0.7f;

        // Blow-off on a throttle lift with boost up.
        if (inst.synth && inst.engineProfile.turbo.enabled &&
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
