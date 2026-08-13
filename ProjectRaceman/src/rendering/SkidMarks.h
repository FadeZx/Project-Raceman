#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace raceman {

class Renderer;

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

    // One call per grounded wheel per frame. `slip` is the tyre's slip magnitude
    // in the same units as SkidMarkSettings::slipThreshold.
    void TrackWheel(const std::string& vehicleId,
                    int wheelIndex,
                    bool grounded,
                    const glm::vec3& contactPosition,
                    const glm::vec3& contactNormal,
                    float slip,
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
    };

    struct WheelTrail {
        glm::vec3 lastEmitPosition{0.0f};
        bool active{false};
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
                  const SkidMarkSettings& settings);
};

} // namespace raceman
