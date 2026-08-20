#pragma once

#include "EngineSynthGenerator.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace raceman {

// Opaque handle to a playing voice. Allocated and owned by AudioManager; the
// pointer stays stable for the life of the voice. Release with StopVoice().
struct AudioVoice;

// Thin wrapper around miniaudio's ma_engine.
// Owned by Application, passed as a raw pointer to SceneEditor (same pattern as Console*).
//
// miniaudio replaced irrKlang because irrKlang's streaming path buffers ~1s of
// audio ahead of the mixer, which makes live procedural synthesis impossible.
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const;

    // Reaps finished one-shots. Call once per frame.
    void Update();

    // 3D listener — call once per frame from the active AudioListener object
    void SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

    // --- tracked voices (caller keeps the handle and must StopVoice it) ---
    AudioVoice* Play3D(const std::string& path, const glm::vec3& position, bool loop = false);
    AudioVoice* Play2D(const std::string& path, bool loop = false);

    // --- procedural voices ---
    AudioVoice* CreateSynthVoice3D(const std::shared_ptr<EngineSynthGenerator>& generator,
                                   const glm::vec3& position);
    AudioVoice* CreateSynthVoice2D(const std::shared_ptr<EngineSynthGenerator>& generator);

    // --- fire and forget (engine owns the lifetime, reaped in Update) ---
    void PlayOneShot2D(const std::string& path, float volume = 1.0f);
    void PlayOneShot3D(const std::string& path, const glm::vec3& position, float volume = 1.0f);

    // --- voice control (all null-safe) ---
    void StopVoice(AudioVoice*& voice);
    void SetVoiceVolume(AudioVoice* voice, float volume);
    void SetVoicePitch(AudioVoice* voice, float pitch);
    void SetVoicePosition(AudioVoice* voice, const glm::vec3& position);
    // spatialBlend 0 = fully 2D, 1 = fully 3D. minDistance/maxDistance in metres.
    void SetVoiceAttenuation(AudioVoice* voice, float minDistance, float maxDistance, float spatialBlend);
    bool IsVoiceFinished(const AudioVoice* voice) const;

    void SetMasterVolume(float volume);
    float GetMasterVolume() const { return masterVolume_; }

    // Optional: warm the resource-manager cache. Play* will auto-load otherwise.
    void Preload(const std::string& path);

    // Measured output buffer depth in milliseconds, or -1 before it is known.
    // This is the parameter-to-ear latency the synth has to live with.
    float GetOutputLatencyMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    AudioVoice* TrackVoice(std::unique_ptr<AudioVoice> voice);

    float masterVolume_{1.0f};
};

} // namespace raceman
