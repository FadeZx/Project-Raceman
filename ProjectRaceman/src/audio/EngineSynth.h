#pragma once

#include "EngineSoundProfile.h"
#include "EngineSynthGenerator.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace raceman {

// -------------------------------------------------------------------------
// Live parameters, written by the game thread and read by the audio thread.
// -------------------------------------------------------------------------
struct EngineSynthParams {
    float rpm{900.0f};
    float idleRpm{900.0f};
    float redlineRpm{7000.0f};
    float load{0.0f};
    float throttle{0.0f};
    float boost{0.0f};
    bool  ignitionCut{false};   // limiter or shift interrupt
    // How much ignition survives the cut. A rev limiter kills it outright, but
    // a paddle upshift only partially interrupts fuel, so the engine keeps some
    // voice and the shift reads as a crack rather than a hole in the sound.
    // Defaults to a FULL cut: if it defaulted to 1.0, setting ignitionCut
    // without also setting this would silently do nothing.
    float ignitionScale{0.0f};
    bool  overrun{false};       // closed throttle above the pops threshold

    // Listener-side perspective, computed on the game thread.
    float exhaustWeight{1.0f};
    float intakeWeight{1.0f};
    float blockWeight{1.0f};
    float lowPass{1.0f};        // 1 = open, lower = more muffled (air / interior)
    // Weights the order bank toward the low orders. Up close you hear each
    // cylinder; from outside the bodywork radiates the low orders and the
    // individual firing detail is lost, so the engine sounds an octave bigger.
    float octaveTilt{0.0f};     // 0 = as authored, 1 = strongly low-order
    float reverbWet{1.0f};      // scales with distance: close is dry, far is wet
    // Acoustics of wherever the LISTENER currently is. Space belongs to the
    // place, not the car, so these come from the scene's environment and any
    // reverb zone the listener is standing in.
    float reverbEarlyGain{-1.0f};   // <0 means "use the profile's own value"
    float reverbEarlySpreadMs{-1.0f};
    float reverbTailGain{-1.0f};
    float reverbTailDecaySeconds{-1.0f};
    float reverbTailDamping{-1.0f};
    float volume{1.0f};
};

// One-shot events that must not be missed between blocks.
enum class EngineSynthEventKind {
    Backfire,
    LimiterPop,
    BlowOff,
    ShiftCut,
};

struct EngineSynthEvent {
    EngineSynthEventKind kind{EngineSynthEventKind::Backfire};
    float strength{1.0f};
};

// -------------------------------------------------------------------------
// Baked profile. Curves are flattened to lookup tables so the audio thread
// never walks a std::vector of control points per sample.
// -------------------------------------------------------------------------
struct EngineSynthBaked {
    static constexpr int kCurveResolution = 48;
    static constexpr int kMaxOrders = 24;
    static constexpr int kMaxCylinders = 16;
    static constexpr int kMaxBanks = 2;
    static constexpr int kMaxMufflerStages = 4;
    // Header primaries are short, so each cylinder gets a compact line.
    // Kept here rather than on EngineSynth so Bake() can clamp against it.
    static constexpr int kRunnerDelaySize = 1024;   // ~21 ms, over 10 m of pipe

    struct BakedCylinder {
        float fireAngleDeg{0.0f};
        int   bankId{0};
        float gain{1.0f};
        float timingJitter{0.0f};
        int   runnerDelay{0};   // this cylinder's own header primary
    };

    struct BakedOrder {
        float order{1.0f};
        float onLoad[kCurveResolution]{};
        float offLoad[kCurveResolution]{};
    };

    float idleRpm{900.0f};
    float redlineRpm{7000.0f};

    BakedCylinder cylinders[kMaxCylinders]{};
    int cylinderCount{0};
    int bankCount{1};
    float cycleDegrees{720.0f};   // 720 for four-stroke, 360 for two-stroke

    BakedOrder orders[kMaxOrders]{};
    int orderCount{0};

    int   runnerDelay[kMaxBanks]{};
    int   collectorDelay{0};
    int   mufflerDelay[kMaxMufflerStages]{};
    int   mufflerStages{0};
    float runnerReflection{0.55f};
    float runnerDamping{0.35f};
    float mufflerReflection{0.45f};
    float tailpipeBrightness{0.60f};

    int   intakeDelay{0};
    float intakeReflection{0.45f};
    float intakeDamping{0.50f};
    float intakeNoise{0.40f};
    float intakePulseGain{0.50f};

    float idleInstability{0.02f};
    float idleInstabilityHz{1.2f};
    float idleLevel{0.20f};
    float combustionVariance{0.08f};
    float combustionDurationMs{3.0f};
    float combustionNoise{0.45f};
    float combustionAttackMs{0.9f};
    float combustionAttackGain{0.55f};

    EngineTurboSettings      turbo;
    EngineDrivetrainSettings drivetrain;
    EngineOverrunSettings    overrun;
    EngineNoiseSettings      noise;
    EngineBodySettings       body;
    EngineRoarSettings       roar;
    EngineReverbSettings     reverb;
    EnginePerspectiveSettings perspective;
    EngineMixSettings        mix;

    // Bakes `profile` for the given rev range and sample rate. Game thread only.
    static std::shared_ptr<const EngineSynthBaked> Bake(const EngineSoundProfile& profile,
                                                        float idleRpm, float redlineRpm,
                                                        int sampleRate);

    float SampleOrderGain(int orderIndex, float rpm, float load) const;
};

// -------------------------------------------------------------------------
// EngineSynth — procedural engine sound in the crank-angle domain.
//
// Rather than pitching a recording, this fires each cylinder at its authored
// crank angle and lets the excitation travel through per-bank exhaust runners,
// a collector, a muffler and a tailpipe. Cylinder count, firing order and bank
// split therefore produce the engine's character directly.
//
// Pure DSP: no audio backend, no glm, no allocation in Render().
// -------------------------------------------------------------------------
class EngineSynth : public EngineSynthGenerator {
public:
    EngineSynth();

    // --- game thread ---
    void SetProfile(const std::shared_ptr<const EngineSynthBaked>& baked);
    void SetParams(const EngineSynthParams& params);
    bool PushEvent(const EngineSynthEvent& event);

    // --- audio thread ---
    void Render(float* output, int frameCount, int sampleRate) override;

    // Diagnostics.
    float GetLastPeak() const { return lastPeak_.load(std::memory_order_relaxed); }
    long long GetFramesRendered() const { return framesRendered_.load(std::memory_order_relaxed); }

private:
    static constexpr int kMaxDelay = 4096;          // ~85 ms at 48 kHz, far beyond any runner
    static constexpr int kEventCapacity = 64;

    struct DelayLine {
        float buffer[kMaxDelay]{};
        int writeIndex{0};
        float lowpassState{0.0f};

        void Reset();
        // Damped, phase-inverting feedback: an exhaust runner is open at one end,
        // which is what gives a pipe its odd-harmonic, hollow character.
        float Process(float input, int delaySamples, float reflection, float damping);
    };

    // State-variable filter. Used as a resonant bandpass for the fixed body
    // resonances; cheap, stable, and trivially retunable per block.
    struct Resonator {
        float low{0.0f};
        float band{0.0f};
        float Process(float input, float f, float q);
        void Reset() { low = 0.0f; band = 0.0f; }
    };

    // Header primaries are short - a metre at 470 m/s is ~100 samples - so a
    // compact line per cylinder is cheap enough to give each its own voice.
    struct ShortDelayLine {
        static constexpr int kSize = EngineSynthBaked::kRunnerDelaySize;
        float buffer[kSize]{};
        int writeIndex{0};
        float lowpassState{0.0f};
        void Reset();
        float Process(float input, int delaySamples, float reflection, float damping);
    };

    struct CylinderState {
        float envelope{0.0f};       // slow blowdown body
        float attackEnvelope{0.0f}; // fast pressure-rise crack
        float lastPhase{0.0f};
    };

    // Compact Schroeder reverb: a few early taps for the ground bounce and
    // nearby surfaces, then allpass diffusion into damped combs for a short
    // tail. Enough to place the engine somewhere real without the cost of a
    // full FDN.
    struct Reverb {
        static constexpr int kEarlySize = 4096;   // ~85 ms at 48 kHz
        static constexpr int kCombCount = 3;
        static constexpr int kCombSize = 4096;
        static constexpr int kAllpassCount = 3;
        static constexpr int kAllpassSize = 1024;

        float early[kEarlySize]{};
        int   earlyWrite{0};
        float comb[kCombCount][kCombSize]{};
        int   combWrite[kCombCount]{};
        float combLowpass[kCombCount]{};
        float allpass[kAllpassCount][kAllpassSize]{};
        int   allpassWrite[kAllpassCount]{};

        void Reset();
        float Process(float input, const int* earlyTaps, const float* earlyGains, int earlyTapCount,
                      const int* combDelays, float combFeedback, float damping,
                      const int* allpassDelays, float earlyGain, float tailGain);
    };

    float NextNoise();
    void ReadParams(EngineSynthParams& out) const;
    void DrainEvents();

    // --- shared state ---
    std::shared_ptr<const EngineSynthBaked> baked_;
    mutable std::atomic<std::uint32_t> paramSeq_{0};
    EngineSynthParams params_{};

    EngineSynthEvent events_[kEventCapacity]{};
    std::atomic<int> eventWrite_{0};
    std::atomic<int> eventRead_{0};

    std::atomic<float> lastPeak_{0.0f};
    std::atomic<long long> framesRendered_{0};

    // --- audio thread only ---
    double crankAngle_{0.0};      // degrees within the cycle
    double revPhase_{0.0};        // 0..1 per crank revolution, drives the order bank
    float  smoothedRpm_{900.0f};
    float  idleWander_{0.0f};
    float  idleWanderTarget_{0.0f};
    float  idleWanderCountdown_{0.0f};

    CylinderState cylinders_[EngineSynthBaked::kMaxCylinders]{};
    ShortDelayLine cylinderRunners_[EngineSynthBaked::kMaxCylinders]{};
    DelayLine runners_[EngineSynthBaked::kMaxBanks]{};
    DelayLine collector_{};
    DelayLine muffler_[EngineSynthBaked::kMaxMufflerStages]{};
    DelayLine intake_{};

    float tailpipePrev_{0.0f};
    float intakePrev_{0.0f};
    float dcPrevIn_{0.0f};
    float dcPrevOut_{0.0f};
    float valveBandpass1_{0.0f};
    float valveBandpass2_{0.0f};

    Resonator bodyResonator1_{};
    Resonator bodyResonator2_{};
    Resonator roarResonator_{};
    float roarHighpass_{0.0f};
    float roarEnvelope_{0.0f};
    float subLowpass_{0.0f};
    float toneLowpass_{0.0f};
    float interiorLowpass_{0.0f};

    Reverb reverb_{};

    float blowOffEnvelope_{0.0f};
    float turboPhase_{0.0f};
    float whinePhase_{0.0f};

    std::uint32_t rngState_{0x9E3779B9u};
};

} // namespace raceman
