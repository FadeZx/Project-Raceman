#pragma once

#include <atomic>

namespace raceman {

// Sample rate every synth voice renders at. The audio engine resamples to the
// device rate if they differ.
inline constexpr int kEngineSynthSampleRate = 48000;

// -------------------------------------------------------------------------
// Generator — the DSP that fills a procedural voice.
//
// Render() runs on the audio thread, NOT the game thread. It must not
// allocate, lock, log, or read anything the game thread mutates except
// through a lock-free channel.
// -------------------------------------------------------------------------
class EngineSynthGenerator {
public:
    virtual ~EngineSynthGenerator() = default;

    // Fill `frameCount` mono samples in [-1, 1]. Must always fill the whole buffer.
    virtual void Render(float* output, int frameCount, int sampleRate) = 0;
};

// Plain sine, used to verify the audio path end to end.
class EngineSynthTestToneGenerator : public EngineSynthGenerator {
public:
    explicit EngineSynthTestToneGenerator(float frequencyHz = 220.0f, float amplitude = 0.25f)
        : frequencyHz_(frequencyHz), amplitude_(amplitude) {}

    void Render(float* output, int frameCount, int sampleRate) override;

    void SetFrequency(float hz) { frequencyHz_.store(hz, std::memory_order_relaxed); }

    // Total frames handed to the device. Compared against playback position this
    // measures output buffer depth, i.e. control latency.
    long long GetFramesRendered() const { return framesRendered_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> frequencyHz_{220.0f};
    float amplitude_{0.25f};
    double phase_{0.0};
    std::atomic<long long> framesRendered_{0};
};

} // namespace raceman
