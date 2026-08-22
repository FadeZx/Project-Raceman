#include "TyreSynth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raceman {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}

float TyreSynth::Resonator::Process(float input, float f, float q) {
    low += f * band;
    const float high = input - low - q * band;
    band += f * high;
    return band;
}

TyreSynth::TyreSynth() {
    for (auto& wheel : wheels_) {
        wheel.rollBand.Reset();
        wheel.squealBand.Reset();
    }
}

void TyreSynth::SetProfile(const std::shared_ptr<const TyreSoundProfile>& profile) {
    std::atomic_store(&profile_, profile);
}

void TyreSynth::SetParams(const TyreSynthParams& params) {
    // Same seqlock as EngineSynth: one writer, one reader, no locking on the
    // audio thread.
    const std::uint32_t seq = paramSeq_.load(std::memory_order_relaxed);
    paramSeq_.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    params_ = params;
    std::atomic_thread_fence(std::memory_order_release);
    paramSeq_.store(seq + 2, std::memory_order_release);
}

void TyreSynth::ReadParams(TyreSynthParams& out) const {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t before = paramSeq_.load(std::memory_order_acquire);
        if (before & 1u) continue;
        std::atomic_thread_fence(std::memory_order_acquire);
        out = params_;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (paramSeq_.load(std::memory_order_acquire) == before) {
            return;
        }
    }
}

float TyreSynth::NextNoise() {
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return static_cast<float>(static_cast<std::int32_t>(rngState_)) * (1.0f / 2147483648.0f);
}

void TyreSynth::Render(float* output, int frameCount, int sampleRate) {
    std::shared_ptr<const TyreSoundProfile> profile = std::atomic_load(&profile_);
    if (!profile || profile->surfaces.empty() || frameCount <= 0) {
        std::memset(output, 0, sizeof(float) * static_cast<std::size_t>((std::max)(0, frameCount)));
        return;
    }

    TyreSynthParams params;
    ReadParams(params);

    const TyreSoundProfile& p = *profile;
    const float sr = static_cast<float>((std::max)(1, sampleRate));
    const float nyquist = sr * 0.5f;
    const int surfaceCount = static_cast<int>(p.surfaces.size());

    const float speed = (std::max)(0.0f, params.speedMps);
    const float speedNorm = Clamp01(speed / (std::max)(1.0f, p.rollSpeedRefMps));
    const float wheelCount = static_cast<float>((std::max)(1, params.wheelCount));

    // Per-wheel, per-block coefficients.
    struct WheelBlock {
        bool active{false};
        float rollF{0.0f}, rollQ{1.0f}, rollHpCoeff{0.0f}, rollGain{0.0f};
        float grainAmount{0.0f}, grainRate{0.0f};
        float rumbleGain{0.0f}, rumbleRate{0.0f};
        float squealF{0.0f}, squealQ{0.2f}, squealTarget{0.0f};
        float squealAlpha{0.0f};
        float impact{0.0f};
    };
    WheelBlock blocks[TyreSynthParams::kMaxWheels]{};

    for (int w = 0; w < params.wheelCount && w < TyreSynthParams::kMaxWheels; ++w) {
        const TyreWheelParams& wheel = params.wheels[w];
        WheelBlock& blk = blocks[w];
        blk.impact = wheel.impact;
        if (!wheel.grounded) {
            // Airborne wheels still get their impact envelope on landing.
            continue;
        }
        blk.active = true;

        // Blend the two surfaces this wheel is straddling, so a wheel crossing
        // onto a kerb is a transition rather than a switch.
        const int a = (std::clamp)(wheel.surfaceA, 0, surfaceCount - 1);
        const int b = (std::clamp)(wheel.surfaceB, 0, surfaceCount - 1);
        const float t = Clamp01(wheel.surfaceBlend);
        const TyreSurfaceSound& sa = p.surfaces[static_cast<std::size_t>(a)];
        const TyreSurfaceSound& sb = p.surfaces[static_cast<std::size_t>(b)];
        auto mix = [t](float x, float y) { return x + (y - x) * t; };

        const float lowHz  = (std::clamp)(mix(sa.rollLowHz, sb.rollLowHz), 20.0f, nyquist * 0.9f);
        const float highHz = (std::clamp)(mix(sa.rollHighHz, sb.rollHighHz), lowHz + 20.0f, nyquist * 0.9f);
        const float centre = std::sqrt(lowHz * highHz);

        blk.rollF = 2.0f * std::sin(3.14159265f * (std::clamp)(centre, 20.0f, nyquist * 0.9f) / sr);
        blk.rollQ = 1.0f / (std::max)(0.35f, centre / (std::max)(1.0f, highHz - lowHz));
        blk.rollHpCoeff = (std::clamp)(kTwoPi * lowHz / sr, 0.001f, 0.9f);

        // Rolling noise rises with speed and with how hard this corner is loaded.
        const float load = Clamp01(wheel.load);
        const float loadTerm = 1.0f - p.rollLoadInfluence + p.rollLoadInfluence * load * 2.0f;
        blk.rollGain = mix(sa.rollGain, sb.rollGain) * speedNorm * loadTerm / wheelCount;

        // Grain: loose material passing under the tread, so its rate follows
        // road speed rather than being a fixed rustle.
        blk.grainAmount = mix(sa.grainAmount, sb.grainAmount);
        blk.grainRate = (std::min)(0.45f, speed * 3.5f * mix(sa.grainRateScale, sb.grainRateScale) / sr);

        // Kerbs are periodic: rumble rate tracks speed.
        blk.rumbleGain = mix(sa.rumbleGain, sb.rumbleGain) * speedNorm / wheelCount;
        blk.rumbleRate = (std::min)(0.45f, mix(sa.rumbleHz, sb.rumbleHz) * speedNorm * 2.0f / sr);

        // Squeal. Slip is continuous, so the tyre sings progressively instead
        // of a sample firing at a threshold.
        const float slipSpan = (std::max)(0.01f, p.squealSlipFull - p.squealSlipThreshold);
        const float slipAmount = Clamp01((wheel.slip - p.squealSlipThreshold) / slipSpan);
        const float squealHz = (std::clamp)(p.squealBaseHz + p.squealRiseHz * slipAmount,
                                            40.0f, nyquist * 0.9f);
        blk.squealF = 2.0f * std::sin(3.14159265f * squealHz / sr);
        blk.squealQ = 1.0f / (std::max)(0.5f, p.squealResonance);
        blk.squealTarget = slipAmount * mix(sa.squealGain, sb.squealGain) * load / wheelCount;

        const float tau = (blk.squealTarget > wheels_[w].squealEnvelope)
            ? (std::max)(0.005f, p.squealAttackSeconds)
            : (std::max)(0.005f, p.squealReleaseSeconds);
        blk.squealAlpha = 1.0f - std::exp(-static_cast<float>(frameCount) / (sr * tau));
    }

    // Impacts are edge-triggered on the game thread; latch them here.
    for (int w = 0; w < params.wheelCount && w < TyreSynthParams::kMaxWheels; ++w) {
        if (blocks[w].impact > 1.0e-4f) {
            wheels_[w].impactEnvelope = (std::max)(wheels_[w].impactEnvelope, blocks[w].impact);
        }
    }

    const float impactDecay = std::exp(-1.0f / (0.055f * sr));
    const float toneTilt = 1.0f - Clamp01(params.lowPass);
    const float toneCoeff = (std::clamp)(kTwoPi * (500.0f + 9000.0f * Clamp01(params.lowPass)) / sr, 0.001f, 0.98f);
    const float master = p.masterVolume * params.volume;

    float peak = 0.0f;

    for (int frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0f;

        for (int w = 0; w < params.wheelCount && w < TyreSynthParams::kMaxWheels; ++w) {
            WheelBlock& blk = blocks[w];
            WheelState& state = wheels_[w];

            if (blk.active) {
                // --- rolling ---
                const float raw = state.rollBand.Process(NextNoise(), blk.rollF, blk.rollQ);
                state.rollHighpass += (raw - state.rollHighpass) * blk.rollHpCoeff;
                float roll = raw - state.rollHighpass;

                if (blk.grainAmount > 1.0e-4f) {
                    state.grainPhase += blk.grainRate;
                    if (state.grainPhase >= 1.0f) state.grainPhase -= 1.0f;
                    // Sharp-ish modulator: stones are discrete, not sinusoidal.
                    const float g = std::sin(kTwoPi * state.grainPhase);
                    roll *= 1.0f - blk.grainAmount * 0.5f * (1.0f - g * g);
                }
                mixed += roll * blk.rollGain * p.rollMasterGain;

                // --- kerb rumble ---
                if (blk.rumbleGain > 1.0e-4f) {
                    state.rumblePhase += blk.rumbleRate;
                    if (state.rumblePhase >= 1.0f) state.rumblePhase -= 1.0f;
                    const float saw = state.rumblePhase * 2.0f - 1.0f;
                    mixed += saw * saw * saw * blk.rumbleGain;
                }

                // --- slip squeal ---
                state.squealEnvelope += (blk.squealTarget - state.squealEnvelope) * blk.squealAlpha;
                if (state.squealEnvelope > 1.0e-4f) {
                    const float sing = state.squealBand.Process(NextNoise(), blk.squealF, blk.squealQ);
                    mixed += sing * state.squealEnvelope * p.squealMasterGain;
                }
            } else {
                state.squealEnvelope *= 0.995f;
            }

            // --- kerb strikes and landings ---
            if (state.impactEnvelope > 1.0e-4f) {
                mixed += NextNoise() * state.impactEnvelope * p.impactGain;
                state.impactEnvelope *= impactDecay;
            }
        }

        // Air absorption, matching the engine's treatment.
        toneLowpass_ += (mixed - toneLowpass_) * toneCoeff;
        if (toneTilt > 0.001f) {
            mixed = mixed + (toneLowpass_ - mixed) * toneTilt;
        }

        mixed *= master;

        const float dcOut = mixed - dcPrevIn_ + 0.9975f * dcPrevOut_;
        dcPrevIn_ = mixed;
        dcPrevOut_ = dcOut;
        mixed = dcOut;

        if (!std::isfinite(mixed)) mixed = 0.0f;
        mixed = (std::clamp)(mixed, -1.0f, 1.0f);
        output[frame] = mixed;

        const float magnitude = std::fabs(mixed);
        if (magnitude > peak) peak = magnitude;
    }

    lastPeak_.store(peak, std::memory_order_relaxed);
}

} // namespace raceman
