#include "SkidMarks.h"

#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace raceman {

namespace {

// Segments overlap slightly so a curved trail does not show gaps between the
// straight boxes that approximate it.
constexpr float kSegmentOverlap = 1.15f;
// Volume thickness along the projection axis. Deep enough to survive a bumpy
// surface and suspension travel, shallow enough not to catch a kerb face.
constexpr float kVolumeThickness = 0.25f;

} // namespace

void SkidMarkSystem::BeginFrame(float deltaSeconds) {
    if (deltaSeconds <= 0.0f) return;
    for (Mark& mark : marks_) {
        mark.age += deltaSeconds;
    }
}

void SkidMarkSystem::PushMark(const glm::vec3& from,
                              const glm::vec3& to,
                              const glm::vec3& normal,
                              float strength,
                              const SkidMarkSettings& settings) {
    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length < 0.0001f) return;

    const glm::vec3 forward = delta / length;
    glm::vec3 up = glm::length(normal) > 0.0001f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(forward, up);
    const float rightLength = glm::length(right);
    // Travel direction parallel to the surface normal means the wheel is moving
    // straight into the ground; there is no meaningful trail direction to use.
    if (rightLength < 0.0001f) return;
    right /= rightLength;
    // Re-orthogonalise so the box is not skewed when the surface is cambered.
    const glm::vec3 alignedForward = glm::cross(up, right);

    Mark mark;
    // Columns are the volume's local axes scaled to its size: local X across the
    // tyre, local Y down the projection axis, local Z along the direction of
    // travel. That matches what decal.fs expects (UVs from local XZ, projection
    // along local -Y).
    mark.transform[0] = glm::vec4(right * (std::max)(settings.width, 0.01f), 0.0f);
    mark.transform[1] = glm::vec4(up * kVolumeThickness, 0.0f);
    mark.transform[2] = glm::vec4(alignedForward * (length * kSegmentOverlap), 0.0f);
    mark.transform[3] = glm::vec4((from + to) * 0.5f, 1.0f);
    mark.strength = (std::clamp)(strength, 0.0f, 1.0f);
    mark.age = 0.0f;
    mark.center = glm::vec3(mark.transform[3]);
    mark.radius = 0.5f * std::sqrt(settings.width * settings.width +
                                   kVolumeThickness * kVolumeThickness +
                                   (length * kSegmentOverlap) * (length * kSegmentOverlap));

    const std::size_t capacity = static_cast<std::size_t>((std::max)(settings.maxMarks, 1));
    if (marks_.size() < capacity) {
        marks_.push_back(mark);
        return;
    }
    // At capacity: recycle the oldest slot. Overwriting in place keeps the cost
    // constant instead of shuffling the whole trail every emission.
    if (writeCursor_ >= marks_.size()) writeCursor_ = 0;
    marks_[writeCursor_] = mark;
    writeCursor_ = (writeCursor_ + 1) % marks_.size();
}

void SkidMarkSystem::TrackWheel(const std::string& vehicleId,
                                int wheelIndex,
                                bool grounded,
                                const glm::vec3& contactPosition,
                                const glm::vec3& contactNormal,
                                float slip,
                                const SkidMarkSettings& settings) {
    if (!settings.enabled) return;

    const std::string key = vehicleId + "#" + std::to_string(wheelIndex);
    WheelTrail& trail = trails_[key];

    if (!grounded) {
        // Break the trail. Without this, a wheel that leaves the ground and lands
        // further down the track draws one long mark bridging the gap.
        trail.active = false;
        return;
    }

    const bool sliding = slip >= settings.slipThreshold;
    if (!sliding) {
        // Keep following the wheel while it grips, so the next mark starts where
        // the slide actually began rather than where the last one ended.
        trail.lastEmitPosition = contactPosition;
        trail.active = true;
        return;
    }

    if (!trail.active) {
        trail.lastEmitPosition = contactPosition;
        trail.active = true;
        return;
    }

    const float travelled = glm::length(contactPosition - trail.lastEmitPosition);
    const float spacing = (std::max)(settings.spacing, 0.02f);
    if (travelled < spacing) return;

    // Strength ramps from the threshold up to roughly twice it, so a gentle slide
    // marks faintly and a full lock-up marks black.
    const float over = (slip - settings.slipThreshold) / (std::max)(settings.slipThreshold, 0.01f);
    const float strength = (std::clamp)(over, 0.15f, 1.0f);

    PushMark(trail.lastEmitPosition, contactPosition, contactNormal, strength, settings);
    trail.lastEmitPosition = contactPosition;
}

void SkidMarkSystem::Submit(Renderer& renderer,
                            const SkidMarkSettings& settings,
                            unsigned int textureId,
                            const glm::vec4 frustumPlanes[6]) const {
    lastSubmittedCount_ = 0;
    if (!settings.enabled) return;

    for (const Mark& mark : marks_) {
        float fade = 1.0f;
        if (settings.fadeSeconds > 0.0f) {
            fade = 1.0f - (std::clamp)(mark.age / settings.fadeSeconds, 0.0f, 1.0f);
            if (fade <= 0.001f) continue;
        }

        if (frustumPlanes != nullptr) {
            bool visible = true;
            for (int plane = 0; plane < 6 && visible; ++plane) {
                const glm::vec4& p = frustumPlanes[plane];
                const float distance = p.x * mark.center.x + p.y * mark.center.y +
                                       p.z * mark.center.z + p.w;
                const float planeLength = glm::length(glm::vec3(p));
                if (planeLength > 0.0001f && distance / planeLength < -mark.radius) visible = false;
            }
            if (!visible) continue;
        }

        DecalDrawCommand decal;
        decal.transform = mark.transform;
        decal.textureId = textureId;
        decal.color = glm::vec4(settings.color, 1.0f);
        decal.opacity = (std::clamp)(settings.opacity * mark.strength * fade, 0.0f, 1.0f);
        // Tighter than the default: a skid mark that climbs a kerb face reads as
        // an obvious projection artefact.
        decal.angleFadeDegrees = 55.0f;
        decal.blendMode = DecalBlendMode::Multiply;
        // Below hand-placed decals, so authored art still sits on top of rubber.
        decal.sortOrder = -100;
        renderer.SubmitDecal(decal);
        ++lastSubmittedCount_;
    }
}

void SkidMarkSystem::Clear() {
    marks_.clear();
    trails_.clear();
    writeCursor_ = 0;
    lastSubmittedCount_ = 0;
}

} // namespace raceman
