#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace raceman {

class Renderer;

struct TyreSmokeSettings {
    bool enabled{true};
    // Normalised contact patch scrub below which a tyre is only marking, not
    // smoking. Higher than the skid mark threshold on purpose: a tyre starts
    // leaving rubber well before it gets hot enough to make visible smoke.
    float slipThreshold{0.40f};
    // Particles per second at full slip, per wheel.
    float spawnRate{55.0f};
    float lifetime{1.7f};
    // Smoke expands as it cools and mixes, so a puff is small where it leaves
    // the tyre and broad by the time it is behind the car.
    float startRadius{0.16f};
    float endRadius{1.30f};
    // Hot rubber smoke rises, but slowly - it is barely buoyant, and smoke that
    // shoots upward reads as steam or a smoke grenade instead.
    float rise{0.85f};
    // How fast a puff gives up the speed it inherited from the wheel. Without
    // this the smoke keeps pace with the car and never gets left behind.
    float drag{1.70f};
    float opacity{0.34f};
    // Hard cap on live particles across every wheel of every car.
    int maxParticles{900};
    glm::vec3 color{0.84f, 0.82f, 0.80f};
};

// Emits and simulates tyre smoke from the same slip the marks are drawn from.
//
// Deliberately CPU side, unlike the rain particles: rain is a procedural swarm
// that can be derived from an instance index and a clock, but smoke has to
// remember where a wheel actually was when it was sliding. A puff outlives the
// moment that made it, which is the whole point of it.
class TyreSmokeSystem {
public:
    // Ages, moves and expands every live puff. Call once per frame before any
    // emission, so a particle spawned this frame is not also aged by it.
    void BeginFrame(float deltaSeconds, const TyreSmokeSettings& settings);

    // One call per wheel per frame. `contactVelocity` is what the puff inherits
    // as it leaves the contact patch - smoke off a sliding tyre is thrown along
    // the slide, not dropped straight down.
    void EmitFromWheel(const std::string& vehicleId,
                       int wheelIndex,
                       bool grounded,
                       const glm::vec3& contactPosition,
                       const glm::vec3& contactNormal,
                       const glm::vec3& contactVelocity,
                       float slipAmount,
                       float smokeGain,
                       const glm::vec3& tint,
                       float deltaSeconds,
                       const TyreSmokeSettings& settings);

    void Submit(Renderer& renderer,
                const TyreSmokeSettings& settings,
                const glm::vec4 frustumPlanes[6]) const;

    void Clear();
    std::size_t LiveParticleCount() const { return particles_.size(); }
    std::size_t LastSubmittedCount() const { return lastSubmittedCount_; }

private:
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 tint{1.0f};
        float age{0.0f};
        float lifetime{1.0f};
        float startRadius{0.2f};
        float endRadius{1.0f};
        float strength{1.0f};
        float rotation{0.0f};
        float spin{0.0f};
    };

    // Fractional particle budget per wheel. Spawn rate is per second, and at
    // 60 Hz a wheel earns well under one particle per frame, so the remainder
    // has to carry or the rate silently rounds to zero.
    struct WheelEmitter {
        float accumulator{0.0f};
    };

    std::vector<Particle> particles_;
    std::unordered_map<std::string, WheelEmitter> emitters_;
    unsigned int randomState_{0x9E3779B9u};
    mutable std::size_t lastSubmittedCount_{0};

    float NextRandom();
    void SpawnParticle(const glm::vec3& position,
                       const glm::vec3& velocity,
                       float strength,
                       const glm::vec3& tint,
                       const TyreSmokeSettings& settings);
};

} // namespace raceman
