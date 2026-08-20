#include "EngineSynth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace raceman {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

int DelaySamplesFor(float lengthMetres, float speedMs, int sampleRate) {
    const float seconds = (std::max)(0.0f, lengthMetres) / (std::max)(1.0f, speedMs);
    const int samples = static_cast<int>(seconds * static_cast<float>(sampleRate));
    return (std::clamp)(samples, 2, 4000);
}

} // namespace

// -------------------------------------------------------------------------
// Baking
// -------------------------------------------------------------------------

std::shared_ptr<const EngineSynthBaked> EngineSynthBaked::Bake(const EngineSoundProfile& profile,
                                                               float idleRpm, float redlineRpm,
                                                               int sampleRate) {
    auto baked = std::make_shared<EngineSynthBaked>();

    baked->idleRpm = (std::max)(1.0f, idleRpm);
    baked->redlineRpm = (std::max)(baked->idleRpm + 1.0f, redlineRpm);
    baked->cycleDegrees = (profile.strokes == 2) ? 360.0f : 720.0f;

    baked->cylinderCount = (std::min)(static_cast<int>(profile.cylinders.size()), kMaxCylinders);
    int highestBank = 0;
    for (int i = 0; i < baked->cylinderCount; ++i) {
        const EngineCylinder& src = profile.cylinders[static_cast<std::size_t>(i)];
        BakedCylinder& dst = baked->cylinders[i];
        dst.fireAngleDeg = std::fmod((std::max)(0.0f, src.fireAngleDeg), baked->cycleDegrees);
        dst.bankId = (std::clamp)(src.bankId, 0, kMaxBanks - 1);
        dst.gain = (std::max)(0.0f, src.gain);
        dst.timingJitter = (std::max)(0.0f, src.timingJitter);
        highestBank = (std::max)(highestBank, dst.bankId);
    }
    baked->bankCount = highestBank + 1;

    baked->orderCount = (std::min)(static_cast<int>(profile.orders.size()), kMaxOrders);
    const float span = baked->redlineRpm - baked->idleRpm;
    for (int i = 0; i < baked->orderCount; ++i) {
        const EngineOrder& src = profile.orders[static_cast<std::size_t>(i)];
        BakedOrder& dst = baked->orders[i];
        dst.order = (std::max)(0.25f, src.order);
        for (int s = 0; s < kCurveResolution; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(kCurveResolution - 1);
            const float rpm = baked->idleRpm + t * span;
            dst.onLoad[s]  = (std::max)(0.0f, SampleEngineCurve(src.gainOnLoad, rpm, 0.0f));
            dst.offLoad[s] = (std::max)(0.0f, SampleEngineCurve(src.gainOffLoad, rpm, 0.0f));
        }
    }

    const EngineExhaustSettings& e = profile.exhaust;
    for (int b = 0; b < kMaxBanks; ++b) {
        // Detune the second bank slightly: two runners of identical length would
        // resonate as one and cancel the point of modelling them separately.
        const float lengthScale = (b == 0) ? 1.0f : 1.06f;
        baked->runnerDelay[b] = DelaySamplesFor(e.runnerLengthM * lengthScale, e.gasSpeedMs, sampleRate);
    }
    baked->collectorDelay = DelaySamplesFor(e.collectorLengthM, e.gasSpeedMs, sampleRate);
    baked->mufflerStages = (std::clamp)(e.mufflerStages, 0, kMaxMufflerStages);
    for (int s = 0; s < baked->mufflerStages; ++s) {
        const float stageScale = 1.0f + 0.37f * static_cast<float>(s);
        baked->mufflerDelay[s] = DelaySamplesFor(e.mufflerLengthM * stageScale, e.gasSpeedMs, sampleRate);
    }
    baked->runnerReflection = (std::clamp)(e.runnerReflection, 0.0f, 0.95f);
    baked->runnerDamping = (std::clamp)(e.runnerDamping, 0.0f, 0.99f);
    baked->mufflerReflection = (std::clamp)(e.mufflerReflection, 0.0f, 0.95f);
    baked->tailpipeBrightness = Clamp01(e.tailpipeBrightness);

    baked->intakeDelay = DelaySamplesFor(profile.intake.lengthM, profile.intake.airSpeedMs, sampleRate);
    baked->intakeReflection = (std::clamp)(profile.intake.reflection, 0.0f, 0.95f);
    baked->intakeDamping = (std::clamp)(profile.intake.damping, 0.0f, 0.99f);
    baked->intakeNoise = (std::max)(0.0f, profile.intake.noise);
    baked->intakePulseGain = (std::max)(0.0f, profile.intake.pulseGain);

    baked->idleInstability = (std::max)(0.0f, profile.idleInstability);
    baked->combustionVariance = Clamp01(profile.combustionVariance);
    baked->combustionDurationMs = (std::clamp)(profile.combustionDurationMs, 0.3f, 40.0f);
    baked->combustionNoise = Clamp01(profile.combustionNoise);
    baked->turbo = profile.turbo;
    baked->drivetrain = profile.drivetrain;
    baked->overrun = profile.overrun;
    baked->noise = profile.noise;
    baked->body = profile.body;
    baked->mix = profile.mix;

    return baked;
}

float EngineSynthBaked::SampleOrderGain(int orderIndex, float rpm, float load) const {
    const float span = (std::max)(1.0f, redlineRpm - idleRpm);
    const float t = Clamp01((rpm - idleRpm) / span) * static_cast<float>(kCurveResolution - 1);
    const int i0 = (std::clamp)(static_cast<int>(t), 0, kCurveResolution - 1);
    const int i1 = (std::min)(i0 + 1, kCurveResolution - 1);
    const float frac = t - static_cast<float>(i0);

    const BakedOrder& order = orders[orderIndex];
    const float on  = order.onLoad[i0]  + (order.onLoad[i1]  - order.onLoad[i0])  * frac;
    const float off = order.offLoad[i0] + (order.offLoad[i1] - order.offLoad[i0]) * frac;
    return off + (on - off) * Clamp01(load);
}

// -------------------------------------------------------------------------
// DelayLine
// -------------------------------------------------------------------------

void EngineSynth::DelayLine::Reset() {
    std::memset(buffer, 0, sizeof(buffer));
    writeIndex = 0;
    lowpassState = 0.0f;
}

float EngineSynth::DelayLine::Process(float input, int delaySamples, float reflection, float damping) {
    const int length = (std::clamp)(delaySamples, 1, kMaxDelay - 1);
    const int readIndex = (writeIndex - length + kMaxDelay) % kMaxDelay;
    const float out = buffer[readIndex];

    // One-pole lowpass in the feedback path: high frequencies lose energy on
    // each bounce, exactly as they do inside a real pipe.
    lowpassState += (out - lowpassState) * (1.0f - damping);

    buffer[writeIndex] = input - reflection * lowpassState;
    writeIndex = (writeIndex + 1) % kMaxDelay;
    return out;
}

float EngineSynth::Resonator::Process(float input, float f, float q) {
    low += f * band;
    const float high = input - low - q * band;
    band += f * high;
    return band;
}

// -------------------------------------------------------------------------
// EngineSynth
// -------------------------------------------------------------------------

EngineSynth::EngineSynth() {
    for (auto& runner : runners_) runner.Reset();
    collector_.Reset();
    for (auto& stage : muffler_) stage.Reset();
    intake_.Reset();
}

void EngineSynth::SetProfile(const std::shared_ptr<const EngineSynthBaked>& baked) {
    std::atomic_store(&baked_, baked);
}

void EngineSynth::SetParams(const EngineSynthParams& params) {
    // Seqlock: bump to odd, write, bump to even. One writer, one reader, no
    // allocation and no chance of the audio thread blocking on a mutex.
    const std::uint32_t seq = paramSeq_.load(std::memory_order_relaxed);
    paramSeq_.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    params_ = params;
    std::atomic_thread_fence(std::memory_order_release);
    paramSeq_.store(seq + 2, std::memory_order_release);
}

void EngineSynth::ReadParams(EngineSynthParams& out) const {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t before = paramSeq_.load(std::memory_order_acquire);
        if (before & 1u) continue; // writer mid-update
        std::atomic_thread_fence(std::memory_order_acquire);
        out = params_;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (paramSeq_.load(std::memory_order_acquire) == before) {
            return;
        }
    }
    // Contended past the retry budget: keep the previous block's values rather
    // than risking a torn read.
}

bool EngineSynth::PushEvent(const EngineSynthEvent& event) {
    const int write = eventWrite_.load(std::memory_order_relaxed);
    const int next = (write + 1) % kEventCapacity;
    if (next == eventRead_.load(std::memory_order_acquire)) {
        return false; // full
    }
    events_[write] = event;
    eventWrite_.store(next, std::memory_order_release);
    return true;
}

void EngineSynth::DrainEvents() {
    const auto* baked = baked_.get();
    int read = eventRead_.load(std::memory_order_relaxed);
    const int write = eventWrite_.load(std::memory_order_acquire);
    while (read != write) {
        const EngineSynthEvent& event = events_[read];
        switch (event.kind) {
            case EngineSynthEventKind::Backfire:
            case EngineSynthEventKind::LimiterPop: {
                // Inject unburnt fuel straight into the exhaust. Because the pop
                // then travels the collector, muffler and tailpipe like any
                // other pulse, it belongs to this exhaust instead of sounding
                // pasted on top of it.
                const float strength = event.strength * (baked ? baked->overrun.gain : 0.7f);
                for (int b = 0; b < (baked ? baked->bankCount : 1); ++b) {
                    runners_[b].buffer[runners_[b].writeIndex] += strength * (0.6f + 0.8f * NextNoise());
                }
                break;
            }
            case EngineSynthEventKind::BlowOff:
                blowOffEnvelope_ = (std::max)(blowOffEnvelope_, event.strength);
                break;
            case EngineSynthEventKind::ShiftCut:
                break; // handled by the ignitionCut parameter
        }
        read = (read + 1) % kEventCapacity;
    }
    eventRead_.store(read, std::memory_order_release);
}

float EngineSynth::NextNoise() {
    // xorshift32 - cheap, no allocation, deterministic per voice.
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return static_cast<float>(static_cast<std::int32_t>(rngState_)) * (1.0f / 2147483648.0f);
}

void EngineSynth::Render(float* output, int frameCount, int sampleRate) {
    std::shared_ptr<const EngineSynthBaked> baked = std::atomic_load(&baked_);
    if (!baked || baked->cylinderCount <= 0 || frameCount <= 0) {
        std::memset(output, 0, sizeof(float) * static_cast<std::size_t>((std::max)(0, frameCount)));
        return;
    }

    EngineSynthParams params;
    ReadParams(params);
    DrainEvents();

    const EngineSynthBaked& b = *baked;
    const float sr = static_cast<float>((std::max)(1, sampleRate));
    const float nyquist = sr * 0.5f;

    // --- per-block derived values -----------------------------------------
    const float targetRpm = (std::clamp)(params.rpm, b.idleRpm, b.redlineRpm * 1.05f);

    // Idle instability: a dead-steady idle reads as synthetic immediately, so
    // wander the target slightly when the engine is near idle and unloaded.
    const float idleProximity = 1.0f - Clamp01((targetRpm - b.idleRpm) / (std::max)(1.0f, b.redlineRpm - b.idleRpm) * 4.0f);
    if (std::fabs(idleWander_ - idleWanderTarget_) < 0.005f) {
        idleWanderTarget_ = NextNoise() * b.idleInstability;
    }
    idleWander_ += (idleWanderTarget_ - idleWander_) * 0.002f;
    const float wanderedRpm = targetRpm * (1.0f + idleWander_ * idleProximity);

    const float startRpm = smoothedRpm_;
    // Interpolate RPM across the block. Stepping it once per block would
    // quantise the crank phase and buzz at the block rate.
    const float rpmStep = (wanderedRpm - startRpm) / static_cast<float>(frameCount);

    const float load = Clamp01(params.load);
    const float ignitionGate = params.ignitionCut ? 0.0f : 1.0f;

    // Order gains, evaluated once per block: RPM moves far too slowly to justify
    // re-walking the curves every sample.
    float orderGain[EngineSynthBaked::kMaxOrders]{};
    for (int i = 0; i < b.orderCount; ++i) {
        orderGain[i] = b.SampleOrderGain(i, wanderedRpm, load);
    }

    const float combustionLevel = 0.35f + 0.65f * load;
    const float intakeLevel = b.intakePulseGain * (0.25f + 0.75f * load);
    // Air still flows on a fuel cut, but the cylinders stop pumping against
    // combustion, so most of the intake roar goes with it. Without this the
    // limiter bounce and shift cut only drop a few dB and read as a wobble
    // rather than as the engine momentarily stopping.
    const float intakeAir = b.intakeNoise * load * Clamp01(wanderedRpm / b.redlineRpm)
                          * (0.25f + 0.75f * ignitionGate);

    const float blowOffDecay = std::exp(-1.0f / ((std::max)(0.01f, b.turbo.blowOffDecaySeconds) * sr));
    // A very short burst is effectively a click; the pipes then ring on it and
    // the result is the classic 'tin can'. Longer pulses put their energy into
    // the low resonances instead.
    const float envelopeDecay = std::exp(-1.0f / ((std::max)(0.0003f, b.combustionDurationMs * 0.001f) * sr));
    const float burstNoise = b.combustionNoise;
    const float burstTone = 1.0f - burstNoise * 0.5f;

    const float valveFreq = (std::clamp)(b.noise.valvetrainFreq, 200.0f, nyquist * 0.9f);
    const float valveCoeff = 2.0f * std::sin(kTwoPi * valveFreq / sr * 0.5f);
    const float valveQ = 1.0f / (std::max)(0.1f, b.noise.valvetrainQ);

    // Fixed body resonances. They do NOT track RPM, which is the point: the
    // firing harmonics sweep through them as the engine revs, and that moving
    // relationship is what makes a real engine sound alive rather than like a
    // siren whose whole spectrum slides up together.
    const float bodyF1 = 2.0f * std::sin(3.14159265f * (std::clamp)(b.body.resonance1Hz, 20.0f, nyquist * 0.9f) / sr);
    const float bodyQ1 = 1.0f / (std::max)(0.25f, b.body.resonance1Q);
    const float bodyF2 = 2.0f * std::sin(3.14159265f * (std::clamp)(b.body.resonance2Hz, 20.0f, nyquist * 0.9f) / sr);
    const float bodyQ2 = 1.0f / (std::max)(0.25f, b.body.resonance2Q);
    const float subCoeff = (std::clamp)(kTwoPi * (std::max)(20.0f, b.body.subCutoffHz) / sr, 0.001f, 0.9f);
    const float toneTilt = Clamp01(b.body.toneTilt);
    // Cutoff falls as tilt rises: 1 keeps only the low end.
    const float toneCoeff = (std::clamp)(kTwoPi * (400.0f + 9000.0f * (1.0f - toneTilt)) / sr, 0.001f, 0.98f);

    const float exhaustBus = b.mix.exhaustGain * params.exhaustWeight;
    const float intakeBus  = b.mix.intakeGain  * params.intakeWeight;
    const float blockBus   = b.mix.blockGain   * params.blockWeight;
    const float drive = (std::max)(0.0f, b.mix.drive);
    const float masterGain = b.mix.masterVolume * params.volume;
    const float lowPassCoeff = Clamp01(params.lowPass);

    float peak = 0.0f;

    // --- per-sample --------------------------------------------------------
    for (int frame = 0; frame < frameCount; ++frame) {
        const float rpm = startRpm + rpmStep * static_cast<float>(frame);
        const float revsPerSecond = rpm / 60.0f;

        // Advance the crank. One full four-stroke cycle is 720 degrees, which is
        // two revolutions, so the cycle completes at half the crank frequency.
        const double degreesPerSample = static_cast<double>(revsPerSecond) * 360.0 / static_cast<double>(sr);
        const double previousAngle = crankAngle_;
        crankAngle_ += degreesPerSample;
        bool wrapped = false;
        if (crankAngle_ >= b.cycleDegrees) {
            crankAngle_ -= b.cycleDegrees;
            wrapped = true;
        }
        revPhase_ += static_cast<double>(revsPerSecond) / static_cast<double>(sr);
        if (revPhase_ >= 1.0) revPhase_ -= 1.0;

        // --- ignition -------------------------------------------------------
        float bankExcitation[EngineSynthBaked::kMaxBanks] = {0.0f, 0.0f};
        float intakeExcitation = 0.0f;

        for (int c = 0; c < b.cylinderCount; ++c) {
            const EngineSynthBaked::BakedCylinder& cyl = b.cylinders[c];
            CylinderState& state = cylinders_[c];

            // Did the crank sweep past this cylinder's firing angle this sample?
            const bool crossed = wrapped
                ? (previousAngle < cyl.fireAngleDeg || crankAngle_ >= cyl.fireAngleDeg)
                : (previousAngle < cyl.fireAngleDeg && crankAngle_ >= cyl.fireAngleDeg);

            if (crossed) {
                // Per-cylinder variance stops the engine sounding like a metronome.
                const float variance = 1.0f + b.combustionVariance * NextNoise();
                state.envelope = cyl.gain * combustionLevel * variance * ignitionGate;
            }

            if (state.envelope > 1.0e-5f) {
                const float burst = state.envelope * (burstTone + burstNoise * NextNoise());
                bankExcitation[cyl.bankId] += burst;
                // Intake draws half a cycle out of phase with the power stroke.
                intakeExcitation += state.envelope * intakeLevel * 0.5f;
                state.envelope *= envelopeDecay;
            }
        }

        // --- exhaust path ---------------------------------------------------
        float collectorInput = 0.0f;
        for (int bank = 0; bank < b.bankCount; ++bank) {
            collectorInput += runners_[bank].Process(bankExcitation[bank], b.runnerDelay[bank],
                                                     b.runnerReflection, b.runnerDamping);
        }
        collectorInput *= (b.bankCount > 1) ? 0.7f : 1.0f;

        float exhaust = collector_.Process(collectorInput, b.collectorDelay,
                                           b.runnerReflection * 0.8f, b.runnerDamping);
        for (int s = 0; s < b.mufflerStages; ++s) {
            exhaust = muffler_[s].Process(exhaust, b.mufflerDelay[s],
                                          b.mufflerReflection, b.runnerDamping * 1.1f);
        }
        // Radiation from the tailpipe differentiates the pressure wave, which is
        // why exhausts are simultaneously bright and boxy.
        float radiated = exhaust - tailpipePrev_ * b.tailpipeBrightness;
        tailpipePrev_ = exhaust;

        // Body resonance and sub weight, added to the radiated exhaust.
        const float bodyRing = bodyResonator1_.Process(radiated, bodyF1, bodyQ1) * b.body.resonance1Gain
                             + bodyResonator2_.Process(radiated, bodyF2, bodyQ2) * b.body.resonance2Gain;
        subLowpass_ += (radiated - subLowpass_) * subCoeff;
        radiated += bodyRing + subLowpass_ * b.body.subGain;

        // --- intake path ----------------------------------------------------
        const float intakeNoiseSample = NextNoise() * intakeAir;
        float intakeSignal = intake_.Process(intakeExcitation + intakeNoiseSample, b.intakeDelay,
                                             b.intakeReflection, b.intakeDamping);
        const float intakeRadiated = intakeSignal - intakePrev_ * 0.35f;
        intakePrev_ = intakeSignal;

        // --- order bank (mechanical/block tone) ------------------------------
        float block = 0.0f;
        for (int i = 0; i < b.orderCount; ++i) {
            const float frequency = b.orders[i].order * revsPerSecond;
            if (frequency >= nyquist * 0.92f) {
                continue; // fade out rather than fold back
            }
            float gain = orderGain[i];
            if (frequency > nyquist * 0.7f) {
                gain *= 1.0f - (frequency - nyquist * 0.7f) / (nyquist * 0.22f);
            }
            if (gain <= 1.0e-4f) continue;
            const float phase = static_cast<float>(revPhase_) * b.orders[i].order;
            block += std::sin(kTwoPi * (phase - std::floor(phase))) * gain;
        }
        block *= 0.25f * ignitionGate;

        // --- valvetrain noise ------------------------------------------------
        const float noiseIn = NextNoise() * b.noise.valvetrainGain * (0.3f + 0.7f * Clamp01(rpm / b.redlineRpm));
        valveBandpass1_ += valveCoeff * valveBandpass2_;
        valveBandpass2_ += valveCoeff * (noiseIn - valveBandpass1_ - valveQ * valveBandpass2_);
        const float valvetrain = valveBandpass2_;

        // --- turbo -----------------------------------------------------------
        float turbo = 0.0f;
        if (b.turbo.enabled) {
            const float whistleFreq = b.turbo.whistleRatio * revsPerSecond;
            if (whistleFreq < nyquist * 0.9f) {
                turboPhase_ += whistleFreq / sr;
                if (turboPhase_ >= 1.0f) turboPhase_ -= 1.0f;
                const float detune = 1.006f;
                turbo = (std::sin(kTwoPi * turboPhase_) +
                         0.6f * std::sin(kTwoPi * turboPhase_ * detune)) *
                        b.turbo.whistleGain * params.boost;
            }
            if (blowOffEnvelope_ > 1.0e-4f) {
                turbo += NextNoise() * blowOffEnvelope_ * b.turbo.blowOffGain;
                blowOffEnvelope_ *= blowOffDecay;
            }
        }

        // --- drivetrain whine -------------------------------------------------
        float whine = 0.0f;
        if (b.drivetrain.whineGain > 1.0e-4f) {
            const float whineFreq = b.drivetrain.whineRatio * revsPerSecond;
            if (whineFreq < nyquist * 0.9f) {
                whinePhase_ += whineFreq / sr;
                if (whinePhase_ >= 1.0f) whinePhase_ -= 1.0f;
                whine = std::sin(kTwoPi * whinePhase_) * b.drivetrain.whineGain * (0.3f + 0.7f * load);
            }
        }

        // --- mix --------------------------------------------------------------
        float mixed = radiated * exhaustBus
                    + intakeRadiated * intakeBus
                    + (block + valvetrain) * blockBus
                    + turbo + whine;

        // Soft clip. tanh-like without the transcendental cost.
        if (drive > 0.0f) {
            const float driven = mixed * (1.0f + drive * 3.0f);
            mixed = driven / (1.0f + std::fabs(driven));
        }

        // Tone tilt: blend toward a low-passed copy. This is the "less sharp"
        // control; it does not just turn the volume down.
        toneLowpass_ += (mixed - toneLowpass_) * toneCoeff;
        if (toneTilt > 0.001f) {
            mixed = mixed + (toneLowpass_ - mixed) * toneTilt;
        }

        mixed *= masterGain;

        // Interior perspective: a one-pole lowpass standing in for the cabin.
        if (lowPassCoeff < 0.999f) {
            interiorLowpass_ += (mixed - interiorLowpass_) * (0.02f + 0.98f * lowPassCoeff);
            mixed = interiorLowpass_;
        }

        // DC blocker. Waveguide feedback and the pulse train both drift.
        const float dcOut = mixed - dcPrevIn_ + 0.9975f * dcPrevOut_;
        dcPrevIn_ = mixed;
        dcPrevOut_ = dcOut;
        mixed = dcOut;

        if (!std::isfinite(mixed)) {
            mixed = 0.0f;
        }
        mixed = (std::clamp)(mixed, -1.0f, 1.0f);
        output[frame] = mixed;

        const float magnitude = std::fabs(mixed);
        if (magnitude > peak) peak = magnitude;
    }

    smoothedRpm_ = wanderedRpm;
    lastPeak_.store(peak, std::memory_order_relaxed);
    framesRendered_.fetch_add(frameCount, std::memory_order_relaxed);
}

} // namespace raceman
