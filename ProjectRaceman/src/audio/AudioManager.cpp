#include "AudioManager.h"

#include <miniaudio/miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace raceman {

// -------------------------------------------------------------------------
// Voice
// -------------------------------------------------------------------------
struct AudioVoice {
    ma_sound sound{};
    bool soundValid{false};

    // Procedural voices only.
    struct SynthSource {
        ma_data_source_base base{};
        EngineSynthGenerator* generator{nullptr};
        float scratch[1024]{};
    };
    std::unique_ptr<SynthSource> synth;
    std::shared_ptr<EngineSynthGenerator> generatorRef; // keeps the DSP alive past the caller

    bool oneShot{false};
};

namespace {

constexpr int kSynthScratchFrames = 1024;

// --- procedural data source ------------------------------------------------

ma_result SynthOnRead(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead) {
    auto* source = static_cast<AudioVoice::SynthSource*>(pDataSource);
    auto* out = static_cast<float*>(pFramesOut);

    ma_uint64 remaining = frameCount;
    while (remaining > 0) {
        const int chunk = static_cast<int>((std::min)(remaining, static_cast<ma_uint64>(kSynthScratchFrames)));
        if (source->generator != nullptr) {
            source->generator->Render(source->scratch, chunk, kEngineSynthSampleRate);
        } else {
            std::memset(source->scratch, 0, sizeof(float) * static_cast<std::size_t>(chunk));
        }
        for (int i = 0; i < chunk; ++i) {
            const float sample = source->scratch[i];
            // Guard the device against a misbehaving generator.
            *out++ = std::isfinite(sample) ? (std::clamp)(sample, -1.0f, 1.0f) : 0.0f;
        }
        remaining -= static_cast<ma_uint64>(chunk);
    }

    if (pFramesRead != nullptr) {
        *pFramesRead = frameCount;
    }
    return MA_SUCCESS;
}

ma_result SynthOnSeek(ma_data_source*, ma_uint64) {
    return MA_SUCCESS; // endless stream, nothing to seek to
}

ma_result SynthOnGetDataFormat(ma_data_source*, ma_format* pFormat, ma_uint32* pChannels,
                               ma_uint32* pSampleRate, ma_channel*, size_t) {
    if (pFormat != nullptr)     *pFormat = ma_format_f32;
    if (pChannels != nullptr)   *pChannels = 1;
    if (pSampleRate != nullptr) *pSampleRate = kEngineSynthSampleRate;
    return MA_SUCCESS;
}

ma_result SynthOnGetCursor(ma_data_source*, ma_uint64* pCursor) {
    if (pCursor != nullptr) *pCursor = 0;
    return MA_SUCCESS;
}

ma_result SynthOnGetLength(ma_data_source*, ma_uint64* pLength) {
    if (pLength != nullptr) *pLength = 0;
    return MA_NOT_IMPLEMENTED; // endless
}

const ma_data_source_vtable kSynthVTable = {
    SynthOnRead,
    SynthOnSeek,
    SynthOnGetDataFormat,
    SynthOnGetCursor,
    SynthOnGetLength,
    nullptr, // onSetLooping
    0,
};

} // namespace

// -------------------------------------------------------------------------
// Impl
// -------------------------------------------------------------------------
struct AudioManager::Impl {
    ma_engine engine{};
    bool engineValid{false};
    std::vector<std::unique_ptr<AudioVoice>> voices;
};

AudioManager::AudioManager() : impl_(std::make_unique<Impl>()) {}

AudioManager::~AudioManager() {
    Shutdown();
}

bool AudioManager::IsInitialized() const {
    return impl_ != nullptr && impl_->engineValid;
}

bool AudioManager::Initialize() {
    if (IsInitialized()) {
        return true; // already up
    }

    ma_engine_config config = ma_engine_config_init();
    config.listenerCount = 1;
    config.sampleRate    = kEngineSynthSampleRate;
    // ~10 ms. Procedural engine sound is driven live, so a shift cut or limiter
    // bounce has to reach the ear while it still matches the picture.
    config.periodSizeInFrames = kEngineSynthSampleRate / 100;

    if (ma_engine_init(&config, &impl_->engine) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] Failed to initialize miniaudio engine.\n");
        return false;
    }
    impl_->engineValid = true;

    std::fprintf(stdout, "[Audio] miniaudio initialized (%u Hz, output latency %.1f ms).\n",
                 ma_engine_get_sample_rate(&impl_->engine), GetOutputLatencyMs());
    return true;
}

void AudioManager::Shutdown() {
    if (!IsInitialized()) {
        return;
    }
    for (auto& voice : impl_->voices) {
        if (!voice) {
            continue;
        }
        if (voice->soundValid) {
            ma_sound_uninit(&voice->sound);
            voice->soundValid = false;
        }
        if (voice->synth) {
            ma_data_source_uninit(&voice->synth->base);
        }
    }
    impl_->voices.clear();

    ma_engine_uninit(&impl_->engine);
    impl_->engineValid = false;
    std::fprintf(stdout, "[Audio] miniaudio shut down.\n");
}

float AudioManager::GetOutputLatencyMs() const {
    if (!IsInitialized()) {
        return -1.0f;
    }
    ma_device* device = ma_engine_get_device(&impl_->engine);
    const ma_uint32 rate = ma_engine_get_sample_rate(&impl_->engine);
    if (device == nullptr || rate == 0) {
        return -1.0f;
    }
    return 1000.0f * static_cast<float>(device->playback.internalPeriodSizeInFrames)
                   * static_cast<float>(device->playback.internalPeriods)
                   / static_cast<float>(rate);
}

void AudioManager::Update() {
    if (!IsInitialized()) {
        return;
    }
    // Reap finished one-shots. Tracked voices stay until the caller stops them.
    for (std::size_t i = 0; i < impl_->voices.size();) {
        AudioVoice* voice = impl_->voices[i].get();
        if (voice != nullptr && voice->oneShot && voice->soundValid &&
            ma_sound_at_end(&voice->sound) == MA_TRUE) {
            ma_sound_uninit(&voice->sound);
            voice->soundValid = false;
            impl_->voices.erase(impl_->voices.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void AudioManager::SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!IsInitialized()) return;
    ma_engine_listener_set_position(&impl_->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
}

AudioVoice* AudioManager::TrackVoice(std::unique_ptr<AudioVoice> voice) {
    AudioVoice* raw = voice.get();
    impl_->voices.push_back(std::move(voice));
    return raw;
}

AudioVoice* AudioManager::Play3D(const std::string& path, const glm::vec3& position, bool loop) {
    if (!IsInitialized() || path.empty()) return nullptr;

    auto voice = std::make_unique<AudioVoice>();
    if (ma_sound_init_from_file(&impl_->engine, path.c_str(), MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, &voice->sound) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] Failed to load '%s'\n", path.c_str());
        return nullptr;
    }
    voice->soundValid = true;
    ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_inverse);
    ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
    ma_sound_start(&voice->sound);
    return TrackVoice(std::move(voice));
}

AudioVoice* AudioManager::Play2D(const std::string& path, bool loop) {
    if (!IsInitialized() || path.empty()) return nullptr;

    auto voice = std::make_unique<AudioVoice>();
    if (ma_sound_init_from_file(&impl_->engine, path.c_str(),
                                MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                nullptr, nullptr, &voice->sound) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] Failed to load '%s'\n", path.c_str());
        return nullptr;
    }
    voice->soundValid = true;
    ma_sound_set_looping(&voice->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&voice->sound);
    return TrackVoice(std::move(voice));
}

AudioVoice* AudioManager::CreateSynthVoice2D(const std::shared_ptr<EngineSynthGenerator>& generator) {
    if (!IsInitialized() || !generator) return nullptr;

    auto voice = std::make_unique<AudioVoice>();
    voice->generatorRef = generator;
    voice->synth = std::make_unique<AudioVoice::SynthSource>();
    voice->synth->generator = generator.get();

    ma_data_source_config sourceConfig = ma_data_source_config_init();
    sourceConfig.vtable = &kSynthVTable;
    if (ma_data_source_init(&sourceConfig, &voice->synth->base) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] Failed to init synth data source.\n");
        return nullptr;
    }

    if (ma_sound_init_from_data_source(&impl_->engine, &voice->synth->base,
                                       MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                       &voice->sound) != MA_SUCCESS) {
        ma_data_source_uninit(&voice->synth->base);
        std::fprintf(stderr, "[Audio] Failed to create synth voice.\n");
        return nullptr;
    }
    voice->soundValid = true;
    ma_sound_start(&voice->sound);
    return TrackVoice(std::move(voice));
}

AudioVoice* AudioManager::CreateSynthVoice3D(const std::shared_ptr<EngineSynthGenerator>& generator,
                                             const glm::vec3& position) {
    AudioVoice* voice = CreateSynthVoice2D(generator);
    if (voice == nullptr) return nullptr;
    ma_sound_set_spatialization_enabled(&voice->sound, MA_TRUE);
    ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_inverse);
    ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
    return voice;
}

void AudioManager::PlayOneShot2D(const std::string& path, float volume) {
    AudioVoice* voice = Play2D(path, /*loop=*/false);
    if (voice != nullptr) {
        voice->oneShot = true;
        ma_sound_set_volume(&voice->sound, volume);
    }
}

void AudioManager::PlayOneShot3D(const std::string& path, const glm::vec3& position, float volume) {
    AudioVoice* voice = Play3D(path, position, /*loop=*/false);
    if (voice != nullptr) {
        voice->oneShot = true;
        ma_sound_set_volume(&voice->sound, volume);
    }
}

void AudioManager::StopVoice(AudioVoice*& voice) {
    if (voice == nullptr || !IsInitialized()) {
        voice = nullptr;
        return;
    }
    for (std::size_t i = 0; i < impl_->voices.size(); ++i) {
        if (impl_->voices[i].get() != voice) {
            continue;
        }
        if (voice->soundValid) {
            ma_sound_stop(&voice->sound);
            ma_sound_uninit(&voice->sound);
            voice->soundValid = false;
        }
        if (voice->synth) {
            ma_data_source_uninit(&voice->synth->base);
        }
        impl_->voices.erase(impl_->voices.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    voice = nullptr;
}

void AudioManager::SetVoiceVolume(AudioVoice* voice, float volume) {
    if (voice == nullptr || !voice->soundValid) return;
    ma_sound_set_volume(&voice->sound, volume);
}

void AudioManager::SetVoicePitch(AudioVoice* voice, float pitch) {
    if (voice == nullptr || !voice->soundValid) return;
    ma_sound_set_pitch(&voice->sound, (std::max)(0.01f, pitch));
}

void AudioManager::SetVoicePosition(AudioVoice* voice, const glm::vec3& position) {
    if (voice == nullptr || !voice->soundValid) return;
    ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
}

void AudioManager::SetVoiceAttenuation(AudioVoice* voice, float minDistance, float maxDistance, float spatialBlend) {
    if (voice == nullptr || !voice->soundValid) return;
    const bool spatial = spatialBlend > 0.0f;
    ma_sound_set_spatialization_enabled(&voice->sound, spatial ? MA_TRUE : MA_FALSE);
    if (!spatial) {
        return;
    }
    ma_sound_set_min_distance(&voice->sound, (std::max)(0.01f, minDistance));
    ma_sound_set_max_distance(&voice->sound, (std::max)(minDistance + 0.01f, maxDistance));
    // Rolloff scales how hard distance bites; a partial spatial blend keeps more
    // of the signal present at range instead of switching spatialisation off.
    ma_sound_set_rolloff(&voice->sound, (std::clamp)(spatialBlend, 0.0f, 1.0f));
}

bool AudioManager::IsVoiceFinished(const AudioVoice* voice) const {
    if (voice == nullptr || !voice->soundValid) return true;
    return ma_sound_at_end(&voice->sound) == MA_TRUE;
}

void AudioManager::SetMasterVolume(float volume) {
    masterVolume_ = volume;
    if (IsInitialized()) {
        ma_engine_set_volume(&impl_->engine, volume);
    }
}

void AudioManager::Preload(const std::string& path) {
    if (!IsInitialized() || path.empty()) return;
    // Decoding once here warms the resource manager cache for later Play* calls.
    ma_sound warm{};
    if (ma_sound_init_from_file(&impl_->engine, path.c_str(), MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, &warm) == MA_SUCCESS) {
        ma_sound_uninit(&warm);
    }
}

} // namespace raceman
