#pragma once

#include <glm/glm.hpp>
#include <string>

// Forward-declare irrKlang types so callers don't need to pull in irrKlang.h
namespace irrklang {
class ISoundEngine;
class ISound;
}

namespace raceman {

// Thin wrapper around irrKlang's ISoundEngine.
// Owned by Application, passed as a raw pointer to SceneEditor (same pattern as Console*).
// All sound handles (ISound*) returned by Play* are owned by irrKlang; callers must call
// drop() on them when done, or just let the engine clean up on Shutdown().
class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return engine_ != nullptr; }

    // 3D listener — call once per frame from the active AudioListener object
    void SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

    // Play a 3D spatialised sound at a world position.
    // Returns nullptr on failure. Caller must call ->drop() when done.
    irrklang::ISound* Play3D(const std::string& path, const glm::vec3& position,
                             bool loop = false, bool startPaused = false);

    // Play a 2D (non-spatialised) sound.
    // Returns nullptr on failure. Caller must call ->drop() when done.
    irrklang::ISound* Play2D(const std::string& path,
                             bool loop = false, bool startPaused = false);

    void SetMasterVolume(float volume);
    float GetMasterVolume() const { return masterVolume_; }

    // Pre-load a sound file into irrKlang's cache. Optional — play will auto-load.
    void Preload(const std::string& path);

    irrklang::ISoundEngine* GetEngine() const { return engine_; }

private:
    irrklang::ISoundEngine* engine_{nullptr};
    float masterVolume_{1.0f};
};

} // namespace raceman
