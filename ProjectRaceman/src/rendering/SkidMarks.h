#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace raceman {

class Renderer;

// The authored half of a skid mark, read from a Decal prefab's component.
// Everything here is per-look; the physical side (when a tyre marks, how far
// apart segments sit, how many survive) stays in SkidMarkSettings.
struct SkidMarkDecalTemplate {
    bool valid{false};
    std::string texturePath;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity{1.0f};
    float angleFadeDegrees{55.0f};
    glm::vec2 uvTiling{1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
    bool alphaBlend{false};
    int sortOrder{-100};
    // From the prefab's Transform scale. 0 means "not authored, use the default".
    float width{0.0f};       // scale X: mark width across the tyre
    float thickness{0.0f};   // scale Y: depth of the projection volume
    // scale Z: metres of track per texture repeat. Without this every segment
    // stretches one full copy of the texture across itself, so a tread pattern
    // changes size with speed and never lines up with its neighbour - which is
    // what makes an untiled trail read as a row of arrows instead of a track.
    float tileLength{0.0f};
};

struct SkidMarkSettings {
    bool enabled{true};
    // Slip magnitude at which a tyre starts leaving rubber. Below this the tyre
    // is gripping and marks nothing.
    float slipThreshold{0.25f};
    // Distance the contact patch must travel before another segment is laid.
    // Distance-based rather than time-based, so mark density does not change
    // with framerate or with how fast the car is going.
    float spacing{0.35f};
    float width{0.28f};
    float opacity{0.75f};
    // Seconds for a mark to fade away. 0 keeps marks for the whole session,
    // which is what a race wants: rubber builds up over a stint.
    float fadeSeconds{0.0f};
    // Hard cap. Each mark is its own draw call, so this is a real performance
    // knob and not just a memory one.
    int maxMarks{300};
    glm::vec3 color{0.06f, 0.055f, 0.05f};
    std::string texturePath;
    // Look sourced from the decal prefab, when one is configured.
    SkidMarkDecalTemplate decal;
};

// One tyre's state for this frame, as the mark system needs to see it.
//
// Passed as a struct rather than a parameter list because what a mark should
// look like depends on several things at once: how hard the tyre is sliding,
// which direction it is sliding in, and what it is sliding on. A tyre locked
// under braking lays a narrow black line; the same tyre sideways in a drift
// lays a wide smear; on grass it lays almost nothing at all.
struct SkidMarkWheelState {
    // 0..1 normalised contact patch scrub. Already zero while the tyre grips.
    float slipAmount{0.0f};
    // Signed longitudinal slip: negative locked, positive spinning. Both lay a
    // narrow concentrated line, which is why the magnitude is what matters.
    float slipRatio{0.0f};
    // Degrees of this wheel's own slip angle. Sideways scrub smears wide.
    float lateralSlipAngle{0.0f};
    // How readily this surface takes rubber. 1 is tarmac, 0 leaves no mark.
    float rubberGain{1.0f};
    // Loose surfaces throw pale dust instead of laying dark rubber, so the
    // mark's colour is pulled toward this by `dustiness`.
    glm::vec3 dustColor{1.0f, 1.0f, 1.0f};
    float dustiness{0.0f};
};

// Lays projected decals along a tyre's contact patch while it is sliding.
//
// Owns nothing GPU-side: it accumulates transforms and hands them to the decal
// pass each frame. That means skid marks inherit everything the decal system
// already does - conforming to uneven track, angle fade over kerbs, appearing in
// wet-road reflections.
class SkidMarkSystem {
public:
    // Call once per frame, before anything submits draws. `deltaSeconds` is only
    // used for fading; emission is purely distance driven.
    void BeginFrame(float deltaSeconds);

    // One call per grounded wheel per frame. `wheel.slipAmount` is compared
    // against SkidMarkSettings::slipThreshold; the rest of the state shapes what
    // the mark looks like once it is being laid.
    void TrackWheel(const std::string& vehicleId,
                    int wheelIndex,
                    bool grounded,
                    const glm::vec3& contactPosition,
                    const glm::vec3& contactNormal,
                    const SkidMarkWheelState& wheel,
                    const SkidMarkSettings& settings);

    // Submits every live mark as a decal. Marks outside `frustumPlanes` are
    // skipped: most of a lap's rubber is behind the camera, and skipping it is
    // what keeps the draw call count survivable.
    void Submit(Renderer& renderer,
                const SkidMarkSettings& settings,
                unsigned int textureId,
                const glm::vec4 frustumPlanes[6]) const;

    void Clear();
    std::size_t LiveMarkCount() const { return marks_.size(); }
    // Marks actually drawn last Submit, i.e. after frustum culling.
    std::size_t LastSubmittedCount() const { return lastSubmittedCount_; }

private:
    struct Mark {
        glm::mat4 transform{1.0f};
        float strength{1.0f};
        float age{0.0f};
        // Bounding sphere for culling, so a mark never needs its corners rebuilt.
        glm::vec3 center{0.0f};
        float radius{0.0f};
        // Baked at emission so the trail's texture continues across segments
        // instead of restarting inside each one.
        glm::vec2 uvTiling{1.0f, 1.0f};
        glm::vec2 uvOffset{0.0f, 0.0f};
        // Baked at emission: the surface the tyre was on when it laid this
        // segment, not the one the car happens to be on now. Driving off a kerb
        // must not recolour the rubber already behind you.
        glm::vec3 dustColor{1.0f, 1.0f, 1.0f};
        float dustiness{0.0f};
    };

    struct WheelTrail {
        glm::vec3 lastEmitPosition{0.0f};
        bool active{false};
        // Metres laid since the trail started, used as the texture's V origin.
        float distance{0.0f};
    };

    std::vector<Mark> marks_;
    // Ring buffer write cursor, used once marks_ reaches maxMarks.
    std::size_t writeCursor_{0};
    std::unordered_map<std::string, WheelTrail> trails_;
    mutable std::size_t lastSubmittedCount_{0};

    void PushMark(const glm::vec3& from,
                  const glm::vec3& to,
                  const glm::vec3& normal,
                  float strength,
                  float widthScale,
                  const SkidMarkWheelState& wheel,
                  float distanceAtFrom,
                  const SkidMarkSettings& settings);
};

} // namespace raceman
