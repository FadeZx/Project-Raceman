#include "SkidMarks.h"

#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace raceman {

namespace {

// Segments overlap slightly so a curved trail does not show gaps between the
// straight boxes that approximate it. Kept small: the blend is multiplicative,
// so an overlap darkens twice and a generous one bands every joint.
constexpr float kSegmentOverlap = 1.05f;
// Softness across the tyre's width. The trail axis gets none - consecutive
// segments share an edge there, and fading both sides of a shared edge is what
// turns a continuous trail into a row of separate rectangles.
constexpr float kEdgeSoftnessAcross = 0.09f;
// Volume thickness along the projection axis. Deep enough to survive a bumpy
// surface and suspension travel, shallow enough not to catch a kerb face.
constexpr float kVolumeThickness = 0.25f;
// Slip angle at which a mark is a full sideways smear rather than a line.
constexpr float kFullSmearAngle = 25.0f;
// Width multipliers for the two ends of that range. A locked wheel lays
// slightly narrower than the tyre (the contact patch concentrates), a full
// drift drags noticeably wider.
constexpr float kLineWidthScale = 0.85f;
constexpr float kSmearWidthScale = 1.50f;

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
                              float widthScale,
                              const SkidMarkWheelState& wheel,
                              float distanceAtFrom,
                              const SkidMarkSettings& settings) {
    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length < 0.0001f) return;

    const glm::vec3 forward = delta / length;
    glm::vec3 up = glm::length(normal) > 0.0001f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    // cross(up, forward), not cross(forward, up): the latter pairs with the
    // re-orthogonalised forward below to give a basis with a negative
    // determinant, and a mirrored volume inverts the winding that the decal
    // pass's glFrontFace(GL_CCW) + glCullFace(GL_FRONT) relies on to draw back
    // faces. Marks then drop out whenever the camera is inside the volume.
    glm::vec3 right = glm::cross(up, forward);
    const float rightLength = glm::length(right);
    // Travel direction parallel to the surface normal means the wheel is moving
    // straight into the ground; there is no meaningful trail direction to use.
    if (rightLength < 0.0001f) return;
    right /= rightLength;
    // Re-orthogonalise so the box is not skewed when the surface is cambered.
    const glm::vec3 alignedForward = glm::cross(right, up);

    const SkidMarkDecalTemplate& tpl = settings.decal;
    // The prefab's scale authors the volume; the profile sliders are the
    // fallback for a project that has not set one up.
    const float width =
        (std::max)((tpl.width > 0.0f ? tpl.width : settings.width) * widthScale, 0.01f);
    const float thickness = (std::max)(tpl.thickness > 0.0f ? tpl.thickness : kVolumeThickness, 0.01f);
    const float span = length * kSegmentOverlap;

    Mark mark;
    // Columns are the volume's local axes scaled to its size: local X across the
    // tyre, local Y down the projection axis, local Z along the direction of
    // travel. That matches what decal.fs expects (UVs from local XZ, projection
    // along local -Y).
    mark.transform[0] = glm::vec4(right * width, 0.0f);
    mark.transform[1] = glm::vec4(up * thickness, 0.0f);
    mark.transform[2] = glm::vec4(alignedForward * span, 0.0f);
    mark.transform[3] = glm::vec4((from + to) * 0.5f, 1.0f);
    mark.strength = (std::clamp)(strength, 0.0f, 1.0f);
    mark.dustColor = wheel.dustColor;
    mark.dustiness = (std::clamp)(wheel.dustiness, 0.0f, 1.0f);
    mark.age = 0.0f;
    mark.center = glm::vec3(mark.transform[3]);
    mark.radius = 0.5f * std::sqrt(width * width + thickness * thickness + span * span);

    // decal.fs reads UVs as (local.xz + 0.5) * tiling + offset, so local X is U
    // (across the tyre) and local Z is V (along travel).
    mark.uvTiling.x = tpl.uvTiling.x;
    mark.uvOffset.x = tpl.uvOffset.x;
    if (tpl.tileLength > 0.0001f) {
        // Fit whole repeats per metre and start each segment where the previous
        // one ended, so the pattern runs continuously down the trail. The extra
        // overlap length is taken off the front, since that is where the segment
        // actually begins.
        const float startDistance = distanceAtFrom - (span - length) * 0.5f;
        const float repeats = tpl.uvTiling.y / tpl.tileLength;
        mark.uvTiling.y = span * repeats;
        // Wrapped to keep the offset small: a long stint would otherwise push it
        // far enough that float precision quantises the texture into steps.
        mark.uvOffset.y = tpl.uvOffset.y + std::fmod(startDistance * repeats, 1.0f);
    } else {
        // No authored repeat length: one copy of the texture per segment, which
        // is the original behaviour and only looks right for a mark whose
        // texture has no direction to it.
        mark.uvTiling.y = tpl.uvTiling.y;
        mark.uvOffset.y = tpl.uvOffset.y;
    }

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
                                const SkidMarkWheelState& wheel,
                                const SkidMarkSettings& settings) {
    if (!settings.enabled) return;

    // A surface that does not take rubber never starts a trail. Sliding across
    // a wall collider should not paint the wall.
    const float rubberGain = (std::clamp)(wheel.rubberGain, 0.0f, 1.0f);
    const float slip = wheel.slipAmount;

    const std::string key = vehicleId + "#" + std::to_string(wheelIndex);
    WheelTrail& trail = trails_[key];

    if (!grounded) {
        // Break the trail. Without this, a wheel that leaves the ground and lands
        // further down the track draws one long mark bridging the gap.
        trail.active = false;
        return;
    }

    const bool sliding = rubberGain > 0.01f && slip >= settings.slipThreshold;
    if (!sliding) {
        // Keep following the wheel while it grips, so the next mark starts where
        // the slide actually began rather than where the last one ended. The
        // texture origin resets with it: each slide is its own trail, and
        // carrying the distance across a gripping stretch would start the next
        // one part-way through the pattern for no reason.
        trail.lastEmitPosition = contactPosition;
        trail.active = true;
        trail.distance = 0.0f;
        return;
    }

    if (!trail.active) {
        trail.lastEmitPosition = contactPosition;
        trail.active = true;
        trail.distance = 0.0f;
        return;
    }

    const float travelled = glm::length(contactPosition - trail.lastEmitPosition);
    const float spacing = (std::max)(settings.spacing, 0.02f);
    if (travelled < spacing) return;

    // Strength ramps from the threshold up to roughly twice it, so a gentle slide
    // marks faintly and a full lock-up marks black.
    const float over = (slip - settings.slipThreshold) / (std::max)(settings.slipThreshold, 0.01f);
    float strength = (std::clamp)(over, 0.15f, 1.0f);

    // Which way the tyre is sliding decides what the mark looks like. A locked
    // or spinning wheel scrubs along its own centreline and lays a narrow,
    // concentrated line; a tyre carrying a big slip angle drags its whole
    // width sideways and smears. Splitting them is what stops a drift and a
    // lock-up leaving the same stripe.
    const float longitudinal = (std::clamp)(std::fabs(wheel.slipRatio), 0.0f, 1.0f);
    const float lateral = (std::clamp)(std::fabs(wheel.lateralSlipAngle) / kFullSmearAngle, 0.0f, 1.0f);
    const float driftFraction = (longitudinal + lateral) > 0.001f
        ? lateral / (longitudinal + lateral)
        : 0.0f;
    // Scaled by the lateral magnitude as well as its share, or every pure
    // sideways slide would smear identically wide however gentle it was: the
    // fraction alone saturates at 1 the moment the slip ratio is zero.
    const float smear = driftFraction * lateral;
    const float widthScale = kLineWidthScale + (kSmearWidthScale - kLineWidthScale) * smear;
    // Spread over more width means less rubber per unit area, so a smear reads
    // lighter than a line laid at the same slip.
    strength *= (1.0f - 0.20f * smear) * rubberGain;

    PushMark(trail.lastEmitPosition, contactPosition, contactNormal, strength, widthScale,
             wheel, trail.distance, settings);
    trail.lastEmitPosition = contactPosition;
    trail.distance += travelled;
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

        const SkidMarkDecalTemplate& tpl = settings.decal;

        DecalDrawCommand decal;
        decal.transform = mark.transform;
        decal.textureId = textureId;
        decal.color = tpl.valid ? tpl.color : glm::vec4(settings.color, 1.0f);
        // Loose surfaces scuff pale dust rather than laying dark rubber. Pulling
        // the colour toward the dust tint keeps one code path for both instead
        // of a second particle-ish system for dirt.
        if (mark.dustiness > 0.001f) {
            decal.color = glm::vec4(
                glm::mix(glm::vec3(decal.color), mark.dustColor, mark.dustiness),
                decal.color.a);
        }
        decal.opacity = (std::clamp)(settings.opacity * (tpl.valid ? tpl.opacity : 1.0f) *
                                     mark.strength * fade, 0.0f, 1.0f);
        // Tighter than the decal default: a skid mark that climbs a kerb face
        // reads as an obvious projection artefact.
        decal.angleFadeDegrees = tpl.valid ? tpl.angleFadeDegrees : 55.0f;
        decal.uvTiling = mark.uvTiling;
        decal.uvOffset = mark.uvOffset;
        decal.edgeFade = glm::vec2(kEdgeSoftnessAcross, 0.0f);
        decal.blendMode = (tpl.valid && tpl.alphaBlend) ? DecalBlendMode::AlphaBlend
                                                        : DecalBlendMode::Multiply;
        // Below hand-placed decals, so authored art still sits on top of rubber.
        decal.sortOrder = tpl.valid ? tpl.sortOrder : -100;
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
