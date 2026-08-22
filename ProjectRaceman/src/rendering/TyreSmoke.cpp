#include "TyreSmoke.h"

#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace raceman {

namespace {

// Fraction of a puff's life spent fading in. Smoke that appears at full opacity
// pops; the contact patch is where it is thinnest in reality anyway.
constexpr float kFadeInFraction = 0.15f;
// How much of the wheel's slide velocity a new puff carries away with it. Not
// all of it: the smoke is air, and air does not keep up with a tyre.
constexpr float kVelocityInheritance = 0.35f;
// Random spread applied to the inherited velocity, in metres per second, so a
// stream of puffs spreads into a plume instead of a line.
constexpr float kScatterSpeed = 0.55f;
// Cap on how much a single frame can emit per wheel. A frame spike must not
// dump the entire particle budget in one place.
constexpr int kMaxSpawnPerWheelPerFrame = 6;

} // namespace

float TyreSmokeSystem::NextRandom() {
    // Small xorshift. A std::mt19937 per frame is far more machinery than
    // scattering a puff needs, and this keeps emission deterministic per run.
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_ & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

void TyreSmokeSystem::BeginFrame(float deltaSeconds, const TyreSmokeSettings& settings) {
    if (!settings.enabled) {
        particles_.clear();
        return;
    }
    if (deltaSeconds <= 0.0f) return;

    const float dragFactor = (std::clamp)(1.0f - settings.drag * deltaSeconds, 0.0f, 1.0f);
    for (std::size_t index = 0; index < particles_.size();) {
        Particle& particle = particles_[index];
        particle.age += deltaSeconds;
        if (particle.age >= particle.lifetime) {
            // Swap-erase: puff order carries no meaning, so there is no reason
            // to shuffle the whole vector every time one expires.
            particles_[index] = particles_.back();
            particles_.pop_back();
            continue;
        }

        particle.velocity *= dragFactor;
        particle.velocity.y += settings.rise * deltaSeconds;
        particle.position += particle.velocity * deltaSeconds;
        particle.rotation += particle.spin * deltaSeconds;
        ++index;
    }
}

void TyreSmokeSystem::SpawnParticle(const glm::vec3& position,
                                    const glm::vec3& velocity,
                                    float strength,
                                    const glm::vec3& tint,
                                    const TyreSmokeSettings& settings) {
    const std::size_t capacity = static_cast<std::size_t>((std::max)(settings.maxParticles, 1));
    Particle particle;
    particle.position = position;
    particle.velocity = velocity;
    particle.tint = tint;
    particle.age = 0.0f;
    // Varied lifetimes stop a burst of puffs all vanishing on the same frame,
    // which is what makes a plume end in a visible straight edge.
    particle.lifetime = (std::max)(0.05f, settings.lifetime * (0.75f + NextRandom() * 0.5f));
    particle.startRadius = (std::max)(0.01f, settings.startRadius * (0.8f + NextRandom() * 0.4f));
    particle.endRadius = (std::max)(particle.startRadius, settings.endRadius * (0.8f + NextRandom() * 0.4f));
    particle.strength = (std::clamp)(strength, 0.0f, 1.0f);
    particle.rotation = NextRandom() * 6.2831853f;
    particle.spin = (NextRandom() - 0.5f) * 1.2f;

    if (particles_.size() < capacity) {
        particles_.push_back(particle);
        return;
    }
    // At capacity, replace the oldest puff rather than dropping the new one.
    // The oldest is the most faded, so it is the one whose loss shows least.
    std::size_t oldest = 0;
    float bestFraction = -1.0f;
    for (std::size_t index = 0; index < particles_.size(); ++index) {
        const float fraction = particles_[index].age / (std::max)(0.001f, particles_[index].lifetime);
        if (fraction > bestFraction) {
            bestFraction = fraction;
            oldest = index;
        }
    }
    particles_[oldest] = particle;
}

void TyreSmokeSystem::EmitFromWheel(const std::string& vehicleId,
                                    int wheelIndex,
                                    bool grounded,
                                    const glm::vec3& contactPosition,
                                    const glm::vec3& contactNormal,
                                    const glm::vec3& contactVelocity,
                                    float slipAmount,
                                    float smokeGain,
                                    const glm::vec3& tint,
                                    float deltaSeconds,
                                    const TyreSmokeSettings& settings) {
    if (!settings.enabled || deltaSeconds <= 0.0f) return;

    const std::string key = vehicleId + "#" + std::to_string(wheelIndex);
    WheelEmitter& emitter = emitters_[key];

    const float gain = (std::clamp)(smokeGain, 0.0f, 1.0f);
    if (!grounded || gain <= 0.01f || slipAmount < settings.slipThreshold) {
        // Drop any part-earned particle. Carrying it across a gripping stretch
        // would fire one puff the instant the tyre slipped again, before it had
        // done enough sliding to deserve it.
        emitter.accumulator = 0.0f;
        return;
    }

    // Ramp from the threshold to full slip, so a tyre on the edge wisps and a
    // locked one pours. Squared because visible smoke rises sharply with how
    // hard the rubber is being worked, not linearly.
    const float over = (slipAmount - settings.slipThreshold) /
                       (std::max)(0.01f, 1.0f - settings.slipThreshold);
    const float intensity = (std::clamp)(over, 0.0f, 1.0f);
    const float strength = intensity * intensity * gain;
    if (strength <= 0.01f) {
        emitter.accumulator = 0.0f;
        return;
    }

    emitter.accumulator += settings.spawnRate * strength * deltaSeconds;
    int spawnCount = static_cast<int>(emitter.accumulator);
    if (spawnCount <= 0) return;
    spawnCount = (std::min)(spawnCount, kMaxSpawnPerWheelPerFrame);
    emitter.accumulator -= static_cast<float>(spawnCount);

    const glm::vec3 up = glm::length(contactNormal) > 0.0001f
        ? glm::normalize(contactNormal)
        : glm::vec3(0.0f, 1.0f, 0.0f);

    for (int spawn = 0; spawn < spawnCount; ++spawn) {
        // Lifted off the surface by its own radius so the billboard is not born
        // half inside the road. Particles are not depth-softened, so a puff
        // spawned exactly on the tarmac would show a hard intersection line.
        const glm::vec3 offset = up * (settings.startRadius * 1.15f);
        const glm::vec3 scatter{
            (NextRandom() - 0.5f) * kScatterSpeed,
            (NextRandom() - 0.5f) * kScatterSpeed * 0.4f,
            (NextRandom() - 0.5f) * kScatterSpeed,
        };
        SpawnParticle(contactPosition + offset,
                      contactVelocity * kVelocityInheritance + scatter,
                      strength,
                      tint,
                      settings);
    }
}

void TyreSmokeSystem::Submit(Renderer& renderer,
                             const TyreSmokeSettings& settings,
                             const glm::vec4 frustumPlanes[6]) const {
    lastSubmittedCount_ = 0;
    if (!settings.enabled) return;

    for (const Particle& particle : particles_) {
        const float life = (std::clamp)(particle.age / (std::max)(0.001f, particle.lifetime), 0.0f, 1.0f);
        const float radius = particle.startRadius + (particle.endRadius - particle.startRadius) * life;

        if (frustumPlanes != nullptr) {
            bool visible = true;
            for (int plane = 0; plane < 6 && visible; ++plane) {
                const glm::vec4& p = frustumPlanes[plane];
                const float distance = p.x * particle.position.x + p.y * particle.position.y +
                                       p.z * particle.position.z + p.w;
                const float planeLength = glm::length(glm::vec3(p));
                if (planeLength > 0.0001f && distance / planeLength < -radius) visible = false;
            }
            if (!visible) continue;
        }

        // Fade in quickly, out slowly. Smoke thins as it expands, so the same
        // amount of rubber spread over a bigger puff has to read as fainter or
        // the plume looks like it gains substance as it drifts away.
        const float fadeIn = (std::clamp)(life / kFadeInFraction, 0.0f, 1.0f);
        const float fadeOut = 1.0f - life;
        const float alpha = (std::clamp)(settings.opacity * particle.strength * fadeIn * fadeOut * fadeOut,
                                         0.0f, 1.0f);
        if (alpha <= 0.002f) continue;

        SmokeParticleDrawCommand command;
        command.position = particle.position;
        command.radius = radius;
        command.color = glm::vec4(settings.color * particle.tint, alpha);
        command.rotation = particle.rotation;
        renderer.SubmitSmokeParticle(command);
        ++lastSubmittedCount_;
    }
}

void TyreSmokeSystem::Clear() {
    particles_.clear();
    emitters_.clear();
    lastSubmittedCount_ = 0;
}

} // namespace raceman
