#pragma once

#include "EngineSynthGenerator.h"
#include "TyreSoundProfile.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace raceman {

// One wheel's contribution, sampled on the game thread.
struct TyreWheelParams {
    bool  grounded{false};
    float load{0.0f};       // 0..1, normalised wheel load
    float slip{0.0f};       // 0..1, how far past the grip limit this tyre is
    float surfaceBlend{0.0f};   // 0..1 across surfaceA -> surfaceB
    int   surfaceA{0};      // TrackSurfaceType index
    int   surfaceB{0};
    float impact{0.0f};     // one-shot strength from a suspension spike
};

struct TyreSynthParams {
    static constexpr int kMaxWheels = 4;

    float speedMps{0.0f};
    TyreWheelParams wheels[kMaxWheels]{};
    int wheelCount{0};

    float lowPass{1.0f};    // air absorption, same idea as the engine
    float volume{1.0f};
};

// -------------------------------------------------------------------------
// TyreSynth — continuous rolling, slip squeal and surface texture.
//
// The old system fired a one-shot when lateral speed crossed a threshold,
// which is why sliding sounded like a sample rather than a tyre. Slip here is
// a continuous quantity: the squeal rises and falls with grip, and each wheel
// carries its own surface so putting two wheels on a kerb is audible.
// -------------------------------------------------------------------------
class TyreSynth : public EngineSynthGenerator {
public:
    TyreSynth();

    // --- game thread ---
    void SetProfile(const std::shared_ptr<const TyreSoundProfile>& profile);
    void SetParams(const TyreSynthParams& params);

    // --- audio thread ---
    void Render(float* output, int frameCount, int sampleRate) override;

    float GetLastPeak() const { return lastPeak_.load(std::memory_order_relaxed); }

private:
    struct Resonator {
        float low{0.0f};
        float band{0.0f};
        float Process(float input, float f, float q);
        void Reset() { low = 0.0f; band = 0.0f; }
    };

    struct WheelState {
        Resonator rollBand{};
        Resonator squealBand{};
        float rollHighpass{0.0f};
        float squealEnvelope{0.0f};
        float grainPhase{0.0f};
        float rumblePhase{0.0f};
        float impactEnvelope{0.0f};
    };

    float NextNoise();
    void ReadParams(TyreSynthParams& out) const;

    std::shared_ptr<const TyreSoundProfile> profile_;
    mutable std::atomic<std::uint32_t> paramSeq_{0};
    TyreSynthParams params_{};
    std::atomic<float> lastPeak_{0.0f};

    WheelState wheels_[TyreSynthParams::kMaxWheels]{};
    float toneLowpass_{0.0f};
    float dcPrevIn_{0.0f};
    float dcPrevOut_{0.0f};
    std::uint32_t rngState_{0x2545F491u};
};

} // namespace raceman
