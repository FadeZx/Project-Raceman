#include "SceneEditorInternal.h"
#include "../audio/AudioManager.h"
#include "../input/InputManager.h"
#include "../physics/PhysicsWorld.h"
#include "../scripting/ScriptRegistry.h"

#include <irrKlang/irrKlang.h>
#include <imgui/imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <thread>

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr float kRuntimeFixedStep = 1.0f / 60.0f;

bool IsEnvironmentFlagEnabled(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    const bool enabled = _dupenv_s(&value, &length, name) == 0 && value != nullptr && std::string(value) == "1";
    free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
#endif
}

} // namespace

void SceneEditor::SetProjectRoot(std::string path) {
#if defined(_WIN32)
    _putenv_s("RACEMAN_PROJECT_ROOT", path.c_str());
#else
    setenv("RACEMAN_PROJECT_ROOT", path.c_str(), 1);
#endif
    if (!scriptsRunning_) {
        LoadProject();
    }
}

std::string SceneEditor::GetProjectRoot() const {
    return FindProjectRoot().string();
}

const PhysicsBuildProgress* SceneEditor::GetPhysicsBuildProgress() const {
    if (playModeLoad_.progress && !playModeLoad_.progress->isDone.load()) {
        return playModeLoad_.progress.get();
    }
    return nullptr;
}

void SceneEditor::StartRuntime() {
    if (scriptsRunning_ || playModeLoad_.phase != PlayModeLoadState::Phase::Idle) {
        return;
    }

    activeViewport_ = SceneEditorActiveViewport::Game;
    scriptsPaused_ = false;

    std::string scriptLoadError;
    if (!LoadScriptAssembly(&scriptLoadError) && console_ != nullptr) {
        console_->AddWarning(scriptLoadError.empty()
            ? "Script DLL was not loaded; continuing without scripts."
            : scriptLoadError);
    }

    playModeScriptAssemblyReady_ = true;
    SetScriptsRunning(true);
}

void SceneEditor::UpdateRuntime(float deltaTime) {
    TickPlayModeLoading();

    UpdateRuntimeSystems(deltaTime);
}

void SceneEditor::UpdateRuntimeSystems(float deltaTime) {
    if (!scriptsRunning_ || deltaTime <= 0.0f) {
        runtimeSimulationAccumulator_ = 0.0f;
        return;
    }

    UpdateScripts(deltaTime);

    constexpr float kMaxAccumulatedFrameTime = 0.10f;
    constexpr int kMaxFixedStepsPerFrame = 4;

    if (scriptsPaused_) {
        runtimeSimulationAccumulator_ = 0.0f;
        CaptureVehicleRuntimeInputActions(false);
    } else {
        const bool routeInput = ShouldRouteInputToGame() && inputManager_ != nullptr;
        CaptureVehicleRuntimeInputActions(routeInput);

        runtimeSimulationAccumulator_ += (std::min)(deltaTime, kMaxAccumulatedFrameTime);

        int fixedSteps = 0;
        while (runtimeSimulationAccumulator_ >= kRuntimeFixedStep && fixedSteps < kMaxFixedStepsPerFrame) {
            UpdateVehiclePhysics(kRuntimeFixedStep);
            UpdatePhysics(kRuntimeFixedStep);
            runtimeSimulationAccumulator_ -= kRuntimeFixedStep;
            ++fixedSteps;
        }

        if (fixedSteps >= kMaxFixedStepsPerFrame && runtimeSimulationAccumulator_ >= kRuntimeFixedStep) {
            runtimeSimulationAccumulator_ = 0.0f;
        }
    }

    UpdateVehicles(deltaTime);
    UpdateCinemachine(deltaTime);
    UpdateAudio(deltaTime);
}

void SceneEditor::StopRuntime() {
    if (scriptsRunning_) {
        SetScriptsRunning(false);
    }
}

void SceneEditor::UpdateScripts(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    for (RuntimeScriptInstance& runtimeScript : runtimeScripts_) {
        auto objectIt = std::find_if(objects_.begin(), objects_.end(), [&](const SceneObject& object) {
            return object.id == runtimeScript.objectId;
        });
        if (objectIt == objects_.end()) {
            continue;
        }
        const int objectIndex = static_cast<int>(std::distance(objects_.begin(), objectIt));
        if (!IsObjectEffectivelyEnabled(objectIndex) || !objectIt->hasScriptComponent || !objectIt->scriptComponent.enabled) {
            continue;
        }
        if (runtimeScript.attachmentIndex >= objectIt->scriptComponent.attachments.size()) {
            continue;
        }

        ObjectScriptAttachment& attachment = objectIt->scriptComponent.attachments[runtimeScript.attachmentIndex];
        if (!attachment.enabled || !runtimeScript.instance) {
            continue;
        }

        InputManager* scriptInput = ShouldRouteInputToGame() ? inputManager_ : nullptr;
        ObjectScriptContext context(*objectIt, &attachment, console_, scriptInput, physicsWorld_.get(), &objects_);
        if (!runtimeScript.started) {
            runtimeScript.instance->OnStart(context);
            runtimeScript.started = true;
        }
        runtimeScript.instance->OnUpdate(context, deltaTime);
    }
}

void SceneEditor::SetScriptsRunning(bool running) {
    if (scriptsRunning_ == running) {
        return;
    }
    runtimeSimulationAccumulator_ = 0.0f;
    // Don't start a new build while one is already in progress.
    if (running && playModeLoad_.phase != PlayModeLoadState::Phase::Idle) {
        return;
    }

    if (running) {
        if (!playModeScriptAssemblyReady_) {
            if (inputManager_ != nullptr) {
                inputManager_->SetWheelForceFeedbackActive(false);
                inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
            }
            ClearScriptRuntime();
            UnloadScriptAssembly();
            SaveCurrentScene();
            profilerStats_ = CollectProfilerStats();
            playModeSnapshot_ = {objects_, selectedIndex_, selectedIndices_};
            hasPlayModeSnapshot_ = true;
            activeViewport_ = SceneEditorActiveViewport::Game;
            scriptsPaused_ = false;
            SyncScriptProjectFiles(false);

            playModeLoad_ = {};
            playModeLoad_.scriptBuild = std::make_shared<PlayModeLoadState::ScriptBuildStatus>();
            playModeLoad_.window = EditorProgressService::Get().Begin(
                "Compiling Scripts", "Building ProjectScripts.dll...", 0, false);
            playModeLoad_.window.SetDetail("Compiling C++ scripts with MSBuild.");
            playModeLoad_.buildStart = std::chrono::high_resolution_clock::now();
            auto status = playModeLoad_.scriptBuild;
            const EditorProgressTask progressWindow = playModeLoad_.window;
            playModeLoad_.scriptBuildThread = std::make_unique<std::thread>([status, progressWindow]() {
                // Give the main thread an opportunity to present the modal
                // before MSBuild begins. Without this handshake, a fast
                // incremental build can finish and transition to scene loading
                // before the first progress frame reaches the screen.
                const auto renderDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                while (!progressWindow.HasBeenRendered() && std::chrono::steady_clock::now() < renderDeadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                std::string error;
                const bool ok = BuildScriptAssembly(&error);
                {
                    std::lock_guard<std::mutex> lock(status->mutex);
                    status->error = std::move(error);
                }
                status->success.store(ok);
                status->isDone.store(true);
            });
            playModeLoad_.phase = PlayModeLoadState::Phase::BuildingScripts;
            return;
        }

        playModeScriptAssemblyReady_ = false;
        std::fprintf(stdout, "[Play] Building scene...\n");
        std::fflush(stdout);

        // scriptsRunning_ is set to true by TickPlayModeLoading once the background build completes.
        RebuildVehicleRuntime();
        std::vector<PhysicsBodyDesc> physicsBodies;
        std::vector<PhysicsCharacterDesc> physicsCharacters;
        BuildRuntimePhysicsDescriptors(physicsBodies, physicsCharacters);
        // Launch async build on background thread.
        std::fprintf(stdout, "[Play] Runtime physics descriptors: %zu bodies, %zu characters, 0 Jolt vehicles (arcade vehicle runtime).\n",
                     physicsBodies.size(),
                     physicsCharacters.size());
        std::fflush(stdout);
        if (IsEnvironmentFlagEnabled("RACEMAN_DISABLE_PLAYER_PHYSICS")) {
            std::fprintf(stdout, "[Play] Player physics disabled by RACEMAN_DISABLE_PLAYER_PHYSICS=1.\n");
            std::fflush(stdout);
            physicsWorld_.reset();
            playModeLoad_ = {};
            RebuildVehicleRuntime();
            RebuildAudioRuntime();
            RebuildScriptRuntime();
            scriptsRunning_ = true;
            if (inputManager_ != nullptr) {
                inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
                inputManager_->SetWheelForceFeedbackActive(false);
            }
            return;
        }
        std::fprintf(stdout, "[Play] Creating physics world...\n");
        std::fflush(stdout);
        playModeLoad_.pendingWorld = std::make_unique<PhysicsWorld>(physicsLayerCollisionMatrix_);
        playModeLoad_.progress = std::make_shared<PhysicsBuildProgress>();
        playModeLoad_.progress->stepsTotal.store(static_cast<int>(physicsBodies.size()));
        playModeLoad_.buildStart = std::chrono::high_resolution_clock::now();
        playModeLoad_.window = EditorProgressService::Get().Begin(
            "Building Scene", "Preparing or baking collision geometry...",
            static_cast<int>(physicsBodies.size()), true);

        PhysicsWorld* worldPtr = playModeLoad_.pendingWorld.get();
        PhysicsBuildProgress* progressPtr = playModeLoad_.progress.get();
        std::fprintf(stdout, "[Play] Starting physics build thread...\n");
        std::fflush(stdout);
        playModeLoad_.buildThread = std::make_unique<std::thread>(
            [worldPtr, progressPtr,
             bodies  = std::move(physicsBodies),
             chars   = std::move(physicsCharacters)]() mutable {
                std::fprintf(stdout, "[Play] Physics build thread started.\n");
                std::fflush(stdout);
                worldPtr->Build(bodies, chars, progressPtr);
            });
        playModeLoad_.phase = PlayModeLoadState::Phase::BuildingPhysics;
        // TickPlayModeLoading() will finalize once the thread completes.
    } else {
        if (inputManager_ != nullptr) {
            inputManager_->SetWheelForceFeedbackActive(false);
            inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
        }
        scriptsRunning_ = false;
        scriptsPaused_ = false;
        playModeScriptAssemblyReady_ = false;
        PlayVehicleSoundStopTriggers();
        ClearAudioRuntime();
        ClearScriptRuntime();
        UnloadScriptAssembly();
        runtimeVehicles_.clear();
        runtimeCinemachineStates_.clear();
        runtimeCameraBrainState_ = {};
        if (physicsWorld_) {
            physicsWorld_->Clear();
            physicsWorld_.reset();
        }
        if (hasPlayModeSnapshot_) {
            objects_ = playModeSnapshot_.objects;
            selectedIndex_ = playModeSnapshot_.selectedIndex;
            selectedIndices_ = playModeSnapshot_.selectedIndices;
            NormalizeSelection();
            playModeSnapshot_ = {};
            hasPlayModeSnapshot_ = false;
        } else {
            ResetPhysicsVelocities();
        }

        // Runtime shutdown unloads the DLL so the next Play build can replace it.
        // Reload it afterward for editor metadata; otherwise the Inspector loses
        // all registered field definitions until the editor is restarted.
        std::string editorScriptLoadError;
        if (!LoadScriptAssembly(&editorScriptLoadError) && console_ != nullptr) {
            console_->AddWarning(editorScriptLoadError.empty()
                ? "Script fields are unavailable because ProjectScripts.dll could not be reloaded."
                : editorScriptLoadError);
        }

        activeViewport_ = SceneEditorActiveViewport::Scene;
        activeGizmoAxis_ = -1;
        hoveredGizmoAxis_ = -1;
        if (console_) {
            console_->AddLog("Play mode stopped.");
        }
        profilerStats_ = CollectProfilerStats();
    }
}

void SceneEditor::SetScriptsPaused(bool paused) {
    if (!scriptsRunning_ || scriptsPaused_ == paused) {
        return;
    }

    scriptsPaused_ = paused;
    runtimeSimulationAccumulator_ = 0.0f;
    if (!scriptsPaused_) {
        activeViewport_ = SceneEditorActiveViewport::Game;
    }
    if (console_) {
        console_->AddLog(scriptsPaused_ ? "Play mode paused." : "Play mode resumed.");
    }
}

void SceneEditor::ClearScriptRuntime() {
    runtimeScripts_.clear();
}

void SceneEditor::RestoreFromPlayModeSnapshot() {
    if (hasPlayModeSnapshot_) {
        objects_ = playModeSnapshot_.objects;
        selectedIndex_ = playModeSnapshot_.selectedIndex;
        selectedIndices_ = playModeSnapshot_.selectedIndices;
        NormalizeSelection();
        playModeSnapshot_ = {};
        hasPlayModeSnapshot_ = false;
    }
    activeViewport_ = SceneEditorActiveViewport::Scene;
    playModeScriptAssemblyReady_ = false;
    if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackActive(false);
        inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
    }
}

void SceneEditor::TickPlayModeLoading() {
    if (playModeLoad_.phase == PlayModeLoadState::Phase::Idle) {
        return;
    }

    if (playModeLoad_.phase == PlayModeLoadState::Phase::BuildingScripts) {
        auto status = playModeLoad_.scriptBuild;
        if (!status || !status->isDone.load()) {
            return;
        }

        if (playModeLoad_.scriptBuildThread && playModeLoad_.scriptBuildThread->joinable()) {
            playModeLoad_.scriptBuildThread->join();
        }

        std::string error;
        {
            std::lock_guard<std::mutex> lock(status->mutex);
            error = status->error;
        }

        if (!status->success.load()) {
            playModeLoad_.window.Fail(error.empty() ? "Script DLL build failed." : error);
            playModeLoad_ = {};
            RestoreFromPlayModeSnapshot();
            const std::string message = error.empty() ? "Script DLL build failed. Check the build output for compiler errors." : error;
            if (console_) {
                console_->AddError(message);
            }
            std::fprintf(stdout, "[Play] Script build failed: %s\n", message.c_str());
            std::fflush(stdout);
            return;
        }

        std::string loadError;
        if (!LoadScriptAssembly(&loadError)) {
            playModeLoad_.window.Fail(loadError.empty() ? "Script DLL load failed." : loadError);
            playModeLoad_ = {};
            RestoreFromPlayModeSnapshot();
            const std::string message = loadError.empty() ? "Script DLL load failed." : loadError;
            if (console_) {
                console_->AddError(message);
            }
            std::fprintf(stdout, "[Play] Script load failed: %s\n", message.c_str());
            std::fflush(stdout);
            return;
        }

        if (console_) {
            console_->AddLog("Loaded script DLL with " + std::to_string(GetRegisteredScripts().size()) + " script(s).");
        }

        playModeLoad_.window.Complete("Scripts compiled and loaded.");
        playModeLoad_ = {};
        playModeScriptAssemblyReady_ = true;
        SetScriptsRunning(true);
        return;
    }

    auto* prog = playModeLoad_.progress.get();
    if (prog && playModeLoad_.window.IsValid()) {
        if (playModeLoad_.window.IsCancellationRequested()) prog->cancelRequested.store(true);
        const int done = prog->stepsDone.load();
        const int total = (std::max)(1, prog->stepsTotal.load());
        playModeLoad_.window.SetProgress(done, total);
        const std::string task = prog->GetTask();
        if (!task.empty()) playModeLoad_.window.SetDetail(task);
    }
    if (!prog || !prog->isDone.load()) {
        return; // still building
    }

    // Build thread has signalled completion — join it.
    if (playModeLoad_.buildThread && playModeLoad_.buildThread->joinable()) {
        playModeLoad_.buildThread->join();
    }

    const bool cancelled = prog->wasCancelled.load();

    if (cancelled) {
        playModeLoad_.window.Cancelled();
        // Discard the partially-built world and restore editor state.
        playModeLoad_ = {};
        RestoreFromPlayModeSnapshot();
        std::fprintf(stdout, "[Play] Build cancelled.\n");
        std::fflush(stdout);
        return;
    }

    // Build succeeded — transfer ownership to the main physics world.
    physicsWorld_ = std::move(playModeLoad_.pendingWorld);

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - playModeLoad_.buildStart).count();
    std::fprintf(stdout, "[Play] Build complete in %.1f ms\n", ms);
    std::fflush(stdout);

    playModeLoad_.window.Complete("Scene build complete.");
    playModeLoad_ = {}; // reset (clears phase to Idle)

    RebuildVehicleRuntime();
    RebuildAudioRuntime();
    RebuildScriptRuntime();
    scriptsRunning_ = true;
    if (inputManager_ != nullptr) {
        inputManager_->SetWheelForceFeedbackState(0.0f, 0.0f, 0.0f);
        inputManager_->SetWheelForceFeedbackActive(true);
    }

    if (console_) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Physics ready (%.1f s). Running.", ms / 1000.0);
        console_->AddLog(buf);
    }
}

void SceneEditor::RenderPlayModeLoadingPopup() {
    // Rendered centrally by EditorProgressService.
}

void SceneEditor::StartCollisionBake(std::vector<std::pair<PhysicsColliderDesc, std::string>> jobs, std::string title) {
    if (jobs.empty()) {
        return;
    }
    if (collisionBake_.active) {
        if (console_) {
            console_->AddWarning("Collision bake is already running.");
        }
        return;
    }

    if (collisionBake_.thread && collisionBake_.thread->joinable()) {
        collisionBake_.thread->join();
    }

    collisionBake_.active = true;
    collisionBake_.title = title.empty() ? "Baking Collision" : std::move(title);
    collisionBake_.window = EditorProgressService::Get().Begin(
        collisionBake_.title, "Preparing or baking collision geometry...",
        static_cast<int>(jobs.size()), true);
    collisionBake_.progress = std::make_shared<PhysicsBuildProgress>();
    collisionBake_.progress->stepsDone.store(0);
    collisionBake_.progress->stepsTotal.store(static_cast<int>(jobs.size()));
    collisionBake_.progress->SetTask("Preparing...");
    collisionBake_.start = std::chrono::high_resolution_clock::now();
    collisionBake_.bakedCount.store(0);
    collisionBake_.failedCount.store(0);
    {
        std::lock_guard<std::mutex> lock(collisionBake_.mutex);
        collisionBake_.lastError.clear();
    }

    auto progress = collisionBake_.progress;
    auto progressWindow = collisionBake_.window;
    auto* bakedCount = &collisionBake_.bakedCount;
    auto* failedCount = &collisionBake_.failedCount;
    auto* errorMutex = &collisionBake_.mutex;
    auto* lastError = &collisionBake_.lastError;
    collisionBake_.thread = std::make_unique<std::thread>(
        [jobs = std::move(jobs), progress, progressWindow, bakedCount, failedCount, errorMutex, lastError]() {
            for (std::size_t i = 0; i < jobs.size(); ++i) {
                if (progress->cancelRequested.load() || progressWindow.IsCancellationRequested()) {
                    progress->wasCancelled.store(true);
                    break;
                }

                const std::string label = jobs[i].second.empty()
                    ? ("Mesh " + std::to_string(i + 1))
                    : jobs[i].second;
                progress->stepsDone.store(static_cast<int>(i));
                progress->SetTask("Baking: " + label);
                progressWindow.SetProgress(static_cast<int>(i), static_cast<int>(jobs.size()));
                progressWindow.SetDetail("Baking: " + label);

                CollisionShapeCacheInfo info;
                const bool baked = PhysicsWorld::BakeCollisionShape(jobs[i].first, &info);
                if (baked) {
                    bakedCount->fetch_add(1);
                } else {
                    failedCount->fetch_add(1);
                    std::lock_guard<std::mutex> lock(*errorMutex);
                    *lastError = label + (info.message.empty() ? "" : (": " + info.message));
                }
            }

            progress->stepsDone.store(static_cast<int>(jobs.size()));
            progress->SetTask(progress->wasCancelled.load() ? "Cancelled." : "Done.");
            progress->isDone.store(true);
            if (progress->wasCancelled.load()) progressWindow.Cancelled();
            else if (failedCount->load() > 0) progressWindow.Fail("Some collision meshes failed. Check the Console.");
            else progressWindow.Complete("Collision bake complete.");
        });
}

void SceneEditor::TickCollisionBake() {
    if (!collisionBake_.active || !collisionBake_.progress || !collisionBake_.progress->isDone.load()) {
        return;
    }

    if (collisionBake_.thread && collisionBake_.thread->joinable()) {
        collisionBake_.thread->join();
    }

    const bool cancelled = collisionBake_.progress->wasCancelled.load();
    const int bakedCount = collisionBake_.bakedCount.load();
    const int failedCount = collisionBake_.failedCount.load();
    std::string lastError;
    {
        std::lock_guard<std::mutex> lock(collisionBake_.mutex);
        lastError = collisionBake_.lastError;
    }

    if (console_) {
        if (cancelled) {
            console_->AddWarning("Collision bake cancelled.");
        } else if (failedCount > 0) {
            console_->AddWarning("Collision bake finished: " + std::to_string(bakedCount) + " baked, " +
                                 std::to_string(failedCount) + " failed." +
                                 (lastError.empty() ? "" : (" Last error: " + lastError)));
        } else {
            console_->AddLog("Collision bake complete: " + std::to_string(bakedCount) + " mesh" +
                             (bakedCount == 1 ? "" : "es") + ".");
        }
    }

    collisionBake_.active = false;
    collisionBake_.title.clear();
    collisionBake_.progress.reset();
    collisionBake_.thread.reset();
    collisionBake_.window = {};
}

void SceneEditor::RenderCollisionBakeInlineStatus() {
    // Progress is rendered as a shared standalone editor window.
}

bool SceneEditor::SyncAttachmentScriptFields(ObjectScriptAttachment& attachment) {
    if (attachment.scriptName.empty()) {
        return false;
    }
    if (FindRegisteredScript(attachment.scriptName) == nullptr) {
        return false;
    }
    const std::vector<ScriptFieldDefinition> definitions = GetRegisteredScriptFieldDefinitions(attachment.scriptName);
    if (definitions.empty()) {
        const bool changed = !attachment.fields.empty();
        attachment.fields.clear();
        return changed;
    }
    return SyncScriptAttachmentFields(attachment, definitions);
}

void SceneEditor::RebuildScriptRuntime() {
    ClearScriptRuntime();

    for (int objectIndex = 0; objectIndex < static_cast<int>(objects_.size()); ++objectIndex) {
        SceneObject& object = objects_[objectIndex];
        if (!IsObjectEffectivelyEnabled(objectIndex) || !object.hasScriptComponent || !object.scriptComponent.enabled) {
            continue;
        }
        for (std::size_t i = 0; i < object.scriptComponent.attachments.size(); ++i) {
            const ObjectScriptAttachment& attachment = object.scriptComponent.attachments[i];
            if (!attachment.enabled || attachment.scriptName.empty()) {
                continue;
            }

            std::unique_ptr<IObjectScript> instance = CreateRegisteredScript(attachment.scriptName);
            if (!instance) {
                if (console_) {
                    console_->AddWarning("Script not registered, rebuild may be required: " + attachment.scriptName);
                }
                continue;
            }
            SyncAttachmentScriptFields(object.scriptComponent.attachments[i]);

            RuntimeScriptInstance runtimeScript;
            runtimeScript.objectId = object.id;
            runtimeScript.attachmentIndex = i;
            runtimeScript.instance = std::move(instance);
            runtimeScripts_.push_back(std::move(runtimeScript));
        }
    }
}

// ---------------------------------------------------------------------------
// Audio runtime
// ---------------------------------------------------------------------------


void SceneEditor::ClearAudioRuntime() {
    // Stop and drop all audio source sounds.
    for (auto& inst : runtimeAudioSources_) {
        if (inst.sound) {
            inst.sound->stop();
            inst.sound->drop();
            inst.sound = nullptr;
        }
    }
    runtimeAudioSources_.clear();

    ClearVehicleSoundRuntime();
}

void SceneEditor::RebuildAudioRuntime() {
    ClearAudioRuntime();
    if (!audioManager_ || !audioManager_->IsInitialized()) return;

    // -- Audio sources with PlayOnAwake --
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& obj = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !obj.hasAudioSource || !obj.audioSource.enabled) continue;
        if (obj.audioSource.clipPath.empty() || !obj.audioSource.playOnAwake) continue;

        const std::string absPath = ProjectAssetPathToAbsolute(obj.audioSource.clipPath).string();
        const glm::vec3 pos = GetObjectWorldPosition(i);
        const bool is3D = obj.audioSource.spatialBlend > 0.5f;
        irrklang::ISound* snd = is3D
            ? audioManager_->Play3D(absPath, pos, obj.audioSource.loop, /*paused=*/false)
            : audioManager_->Play2D(absPath, obj.audioSource.loop, /*paused=*/false);
        if (snd) {
            snd->setVolume(obj.audioSource.volume);
            snd->setPlaybackSpeed(obj.audioSource.pitch);
            if (is3D) {
                snd->setMinDistance(obj.audioSource.minDistance);
            }
            RuntimeAudioSourceInstance inst;
            inst.objectId = obj.id;
            inst.sound    = snd;
            runtimeAudioSources_.push_back(std::move(inst));
        }
    }

    RebuildVehicleSoundRuntime();
}

void SceneEditor::UpdateAudio(float deltaTime) {
    if (!audioManager_ || !audioManager_->IsInitialized()) return;
    if (!scriptsRunning_ || scriptsPaused_) return;

    // -- Update AudioListener position (first enabled one wins) --
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& obj = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !obj.hasAudioListener || !obj.audioListener.enabled) continue;
        const glm::mat4 worldMat = GetObjectWorldMatrix(i);
        const glm::vec3 pos     = glm::vec3(worldMat[3]);
        const glm::vec3 forward = glm::normalize(glm::vec3(-worldMat[2]));
        const glm::vec3 up      = glm::normalize(glm::vec3(worldMat[1]));
        audioManager_->SetListenerTransform(pos, forward, up);
        break;
    }

    // -- Update 3D audio source positions --
    for (auto& inst : runtimeAudioSources_) {
        if (!inst.sound || inst.sound->isFinished()) continue;
        const int idx = FindObjectIndexById(inst.objectId);
        if (idx < 0) continue;
        const AudioSourceComponent& source = objects_[idx].audioSource;
        inst.sound->setVolume(source.volume);
        inst.sound->setPlaybackSpeed(source.pitch);
        if (source.spatialBlend > 0.5f) {
            const glm::vec3 pos = GetObjectWorldPosition(idx);
            inst.sound->setPosition(irrklang::vec3df(pos.x, pos.y, pos.z));
            inst.sound->setMinDistance(source.minDistance);
        }
    }

    UpdateVehicleSoundRuntime(deltaTime);
}

void SceneEditor::HandleConsoleCommand(const std::string& command) {
    const std::string trimmed = TrimCopyLocal(command);
    if (trimmed.empty()) {
        return;
    }

    if (trimmed == "help" || trimmed == "script.help") {
        if (console_) {
            console_->AddLog("Commands: script.help, script.list, script.run, script.pause, script.stop");
            console_->AddLog("Script callbacks: void OnStart(ObjectScriptContext& context), void OnUpdate(ObjectScriptContext& context, float deltaTime)");
            console_->AddLog("Object: context.GetObjectName(), context.GetTag(), context.SetTag(\"Player\"), context.CompareTag(\"Enemy\")");
            console_->AddLog("Find: auto enemy = context.FindObjectWithTag(\"Enemy\"); auto camera = context.FindObjectByName(\"Main Camera\");");
            console_->AddLog("ObjectHandle: IsValid(), GetObjectName(), GetTag(), GetPosition(), SetPosition(vec3), SetEnabled(bool)");
            console_->AddLog("Transform: context.GetPosition(), context.SetPosition({0, 2, 0}), context.GetForwardVector()");
            console_->AddLog("Input: context.GetAxis(\"moveX\"), context.GetAxis(\"moveY\"), context.IsActionDown(\"jump\")");
            console_->AddLog("Rigidbody: context.IsRigidbodyDynamic(), context.SetRigidbodyVelocity({0, 0, -5}), context.AddRigidbodyImpulse({0, 4, 0})");
            console_->AddLog("Collider: context.HasCollider(), context.SetColliderEnabled(false), context.SetColliderTrigger(true), context.SetBoxColliderSize({1, 2, 1})");
            console_->AddLog("Camera: context.HasCamera(), context.Camera().SetFieldOfView(60.0f)");
            console_->AddLog("Light: context.HasLight(), context.SetLightColor({1, 0.8f, 0.4f}), context.SetLightIntensity(3.0f)");
            console_->AddLog("AudioSource: context.HasAudioSource(), context.SetAudioVolume(0.5f), context.SetAudioPitch(1.2f)");
            console_->AddLog("Fields: RACEMAN_SCRIPT_FIELD_FLOAT(\"moveSpeed\", \"Move Speed\", 8.0f), then context.GetFloatField(\"moveSpeed\", 8.0f)");
        }
        return;
    }
    if (trimmed == "script.run") {
        if (scriptsRunning_) {
            SetScriptsPaused(false);
        } else {
            SetScriptsRunning(true);
        }
        return;
    }
    if (trimmed == "script.pause") {
        SetScriptsPaused(true);
        return;
    }
    if (trimmed == "script.stop") {
        SetScriptsRunning(false);
        return;
    }
    if (trimmed == "script.list") {
        if (!console_) {
            return;
        }
        const auto& scripts = GetRegisteredScripts();
        if (scripts.empty()) {
            console_->AddLog("No registered scripts. Press Play to build/load scripts, or create a script first.");
            return;
        }
        for (const ScriptDescriptor& script : scripts) {
            console_->AddLog(script.name + " (" + script.path + ")");
        }
        return;
    }

    if (console_) {
        console_->AddWarning("Unknown command: " + trimmed);
    }
}

void SceneEditor::UpdateCinemachine(float deltaTime) {
    if (!scriptsRunning_ || scriptsPaused_ || deltaTime <= 0.0f) {
        return;
    }

    auto findById  = [this](const std::string& id) { return FindObjectIndexById(id); };
    auto getMatrix = [this](int idx) { return GetObjectWorldMatrix(idx); };

    for (int camIdx = 0; camIdx < static_cast<int>(objects_.size()); ++camIdx) {
        SceneObject& camObj = objects_[camIdx];
        if (!IsObjectEffectivelyEnabled(camIdx)) {
            continue;
        }
        if (!camObj.hasCamera || !camObj.camera.enabled) {
            continue;
        }
        if (!camObj.hasCinemachine) {
            continue;
        }

        const CinemachineCameraComponent& cine = camObj.cinemachine;

        glm::mat4 desiredWorld(1.0f);
        const bool hasFollowOrLookAtTarget = cine.enabled && ComputeCinemachineDesiredWorldMatrix(
            cine, camIdx, objects_, findById, getMatrix, desiredWorld);
        if (!hasFollowOrLookAtTarget) {
            // Targetless virtual cameras are valid static/parented cameras.
            // Their hierarchy world transform is already the desired pose.
            desiredWorld = getMatrix(camIdx);
        }

        const glm::vec3 desiredPos = glm::vec3(desiredWorld[3]);
        const glm::quat desiredRot = glm::normalize(glm::quat_cast(desiredWorld));
        const int followTargetIndex = cine.enabled && cine.type != CinemachineCameraType::LookAt &&
            !cine.followTargetId.empty() ? findById(cine.followTargetId) : -1;
        const bool hasFollowTarget = followTargetIndex >= 0 && followTargetIndex != camIdx;
        const glm::vec3 followTargetPosition = hasFollowTarget
            ? glm::vec3(getMatrix(followTargetIndex)[3])
            : glm::vec3(0.0f);

        // Seed smoothing state on first touch
        RuntimeCinemachineState& state = runtimeCinemachineStates_[camObj.id];
        if (!hasFollowOrLookAtTarget) {
            // A child cockpit/hood camera must remain rigidly attached to its
            // parent. Damping here would make it lag behind the vehicle.
            state.smoothedPosition = desiredPos;
            state.smoothedRotation = desiredRot;
            state.followTargetId.clear();
            state.followTargetInitialized = false;
            state.initialized = true;
            continue;
        }
        if (!state.initialized) {
            const glm::mat4 camWorldMatrix = getMatrix(camIdx);
            state.smoothedPosition = glm::vec3(camWorldMatrix[3]);
            state.smoothedRotation = glm::normalize(glm::quat_cast(camWorldMatrix));
            state.initialized = true;
        }

        // Carry target translation directly into the camera before damping the
        // relative offset. Smoothing absolute world position made a fast car
        // repeatedly lag and catch up to its already-interpolated render pose.
        if (hasFollowTarget) {
            if (state.followTargetInitialized && state.followTargetId == cine.followTargetId) {
                state.smoothedPosition += followTargetPosition - state.previousFollowTargetPosition;
            }
            state.previousFollowTargetPosition = followTargetPosition;
            state.followTargetId = cine.followTargetId;
            state.followTargetInitialized = true;
        } else {
            state.followTargetId.clear();
            state.followTargetInitialized = false;
        }

        // Zero damping means a rigid/direct follow, matching editor wording.
        const float posT = cine.positionDamping <= 0.0f
            ? 1.0f
            : 1.0f - std::exp(-cine.positionDamping * deltaTime);
        const float rotT = cine.rotationDamping <= 0.0f
            ? 1.0f
            : 1.0f - std::exp(-cine.rotationDamping * deltaTime);

        state.smoothedPosition = glm::mix(state.smoothedPosition, desiredPos, posT);
        state.smoothedRotation = glm::normalize(glm::slerp(state.smoothedRotation, desiredRot, rotT));
    }

    // The main Camera acts as the brain. It renders the enabled virtual camera
    // with the highest priority and blends whenever that selection changes.
    int activeVirtualIndex = -1;
    int activePriority = (std::numeric_limits<int>::min)();
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const SceneObject& candidate = objects_[i];
        if (!IsObjectEffectivelyEnabled(i) || !candidate.hasCamera || !candidate.camera.enabled ||
            !candidate.hasCinemachine) {
            continue;
        }
        const auto stateIt = runtimeCinemachineStates_.find(candidate.id);
        if (stateIt == runtimeCinemachineStates_.end() || !stateIt->second.initialized) {
            continue;
        }
        if (activeVirtualIndex < 0 || candidate.cinemachine.priority > activePriority) {
            activeVirtualIndex = i;
            activePriority = candidate.cinemachine.priority;
        }
    }

    if (activeVirtualIndex >= 0) {
        const SceneObject& active = objects_[activeVirtualIndex];
        const RuntimeCinemachineState& target = runtimeCinemachineStates_.at(active.id);
        RuntimeCameraBrainState& brain = runtimeCameraBrainState_;
        const bool selectionChanged = brain.activeVirtualCameraId != active.id;
        if (!brain.initialized) {
            brain.position = target.smoothedPosition;
            brain.rotation = target.smoothedRotation;
            brain.fieldOfView = active.camera.fieldOfViewDegrees;
            brain.initialized = true;
        } else if (selectionChanged) {
            brain.blendStartPosition = brain.position;
            brain.blendStartRotation = brain.rotation;
            brain.blendStartFieldOfView = brain.fieldOfView;
            brain.blendElapsed = 0.0f;
            brain.blendDuration = (std::max)(0.0f, active.cinemachine.blendDuration);
        }
        brain.activeVirtualCameraId = active.id;
        brain.nearClip = active.camera.nearClip;
        brain.farClip = active.camera.farClip;
        brain.clearColor = active.camera.clearColor;

        if (brain.blendElapsed < brain.blendDuration && brain.blendDuration > 0.0001f) {
            brain.blendElapsed = (std::min)(brain.blendDuration, brain.blendElapsed + deltaTime);
            const float linearT = brain.blendElapsed / brain.blendDuration;
            const float blendT = linearT * linearT * (3.0f - 2.0f * linearT);
            brain.position = glm::mix(brain.blendStartPosition, target.smoothedPosition, blendT);
            brain.rotation = glm::normalize(glm::slerp(brain.blendStartRotation, target.smoothedRotation, blendT));
            brain.fieldOfView = glm::mix(brain.blendStartFieldOfView, active.camera.fieldOfViewDegrees, blendT);
        } else {
            brain.position = target.smoothedPosition;
            brain.rotation = target.smoothedRotation;
            brain.fieldOfView = active.camera.fieldOfViewDegrees;
        }
    } else {
        runtimeCameraBrainState_ = {};
    }

    // Clear states for cameras that no longer exist
    for (auto it = runtimeCinemachineStates_.begin(); it != runtimeCinemachineStates_.end(); ) {
        const int idx = FindObjectIndexById(it->first);
        if (idx < 0) {
            it = runtimeCinemachineStates_.erase(it);
        } else {
            ++it;
        }
    }
}

void SceneEditor::PreviewCinemachineInEditor() {
}

} // namespace raceman
