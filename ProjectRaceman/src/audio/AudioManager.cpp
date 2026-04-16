#include "AudioManager.h"

#include <irrKlang/irrKlang.h>
#include <cstdio>

namespace raceman {

AudioManager::~AudioManager() {
    Shutdown();
}

bool AudioManager::Initialize() {
    if (engine_) {
        return true; // already up
    }
    engine_ = irrklang::createIrrKlangDevice();
    if (!engine_) {
        std::fprintf(stderr, "[Audio] Failed to create irrKlang device.\n");
        return false;
    }
    engine_->setDefault3DSoundMinDistance(3.0f);
    engine_->setRolloffFactor(1.0f);
    std::fprintf(stdout, "[Audio] irrKlang initialized.\n");
    return true;
}

void AudioManager::Shutdown() {
    if (engine_) {
        engine_->stopAllSounds();
        engine_->drop();
        engine_ = nullptr;
        std::fprintf(stdout, "[Audio] irrKlang shut down.\n");
    }
}

void AudioManager::SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!engine_) return;
    engine_->setListenerPosition(
        irrklang::vec3df(position.x, position.y, position.z),
        irrklang::vec3df(forward.x,  forward.y,  forward.z),
        irrklang::vec3df(0.0f, 0.0f, 0.0f), // velocity (unused)
        irrklang::vec3df(up.x, up.y, up.z));
}

irrklang::ISound* AudioManager::Play3D(const std::string& path, const glm::vec3& position,
                                        bool loop, bool startPaused) {
    if (!engine_ || path.empty()) return nullptr;
    return engine_->play3D(path.c_str(),
                           irrklang::vec3df(position.x, position.y, position.z),
                           loop, startPaused, /*track=*/true);
}

irrklang::ISound* AudioManager::Play2D(const std::string& path, bool loop, bool startPaused) {
    if (!engine_ || path.empty()) return nullptr;
    return engine_->play2D(path.c_str(), loop, startPaused, /*track=*/true);
}

void AudioManager::SetMasterVolume(float volume) {
    masterVolume_ = volume;
    if (engine_) {
        engine_->setSoundVolume(volume);
    }
}

void AudioManager::Preload(const std::string& path) {
    if (!engine_ || path.empty()) return;
    engine_->addSoundSourceFromFile(path.c_str(), irrklang::ESM_AUTO_DETECT, true);
}

} // namespace raceman
