#pragma once

#include <functional>
#include <string>
#include <vector>

namespace raceman {

// -------------------------------------------------------------------------
// Engine layer — one looping clip whose pitch and volume are driven by RPM.
// Multiple layers blend together to produce realistic engine sound.
// -------------------------------------------------------------------------
struct VehicleSoundEngineLayer {
    std::string clipPath;           // e.g. "assets/audio/engine_mid.ogg"
    float rpmMin{0.0f};             // layer fades in above this RPM
    float rpmMax{8000.0f};          // layer fades out above this RPM
    float pitchAtRpmMin{0.8f};      // playback speed at rpmMin (1.0 = original)
    float pitchAtRpmMax{1.6f};      // playback speed at rpmMax
    float volumeAtRpmMin{0.0f};     // volume at rpmMin  (0–1)
    float volumeAtRpmMax{1.0f};     // volume at rpmMax
    float volumeThrottleScale{0.0f};// extra volume per unit of throttle (adds engine-load feel)
};

// -------------------------------------------------------------------------
// Trigger — one-shot non-looping sound fired on a discrete event.
// -------------------------------------------------------------------------
enum class VehicleSoundTrigger {
    GearUp,        // gear shifted up
    GearDown,      // gear shifted down
    Backfire,      // throttle lift at high RPM
    EngineStart,   // play mode begins
    EngineStop,    // play mode ends
    TireSqueal,    // high lateral G / drift
};

struct VehicleSoundTriggerEntry {
    std::string clipPath;
    VehicleSoundTrigger trigger{VehicleSoundTrigger::GearUp};
    float volume{1.0f};
    // Backfire only fires when RPM is above this threshold on throttle lift.
    float minRpmForBackfire{4000.0f};
    // TireSqueal only fires when lateral speed exceeds this (m/s).
    float minLateralSpeedForSqueal{4.0f};
};

// -------------------------------------------------------------------------
// Full profile — saved as   <name>.vehiclesound.json
// -------------------------------------------------------------------------
struct VehicleSoundProfile {
    std::string name{"default"};
    std::vector<VehicleSoundEngineLayer>  engineLayers;
    std::vector<VehicleSoundTriggerEntry> triggerSounds;
    float masterVolume{1.0f};
    float spatialBlend{1.0f};   // 0 = 2D, 1 = fully 3D
    float minDistance{3.0f};    // irrKlang min distance (full volume)
    float maxDistance{80.0f};   // irrKlang max distance (silence)
};

// -------------------------------------------------------------------------
// Loader — mirrors VehicleConfigLoader pattern
// -------------------------------------------------------------------------
class VehicleSoundProfileLoader {
public:
    static VehicleSoundProfile loadFromFile(const std::string& path);
    static bool saveToFile(const std::string& path, const VehicleSoundProfile& profile,
                           std::string* outError = nullptr);
    static VehicleSoundProfile makeDefault();
};

} // namespace raceman
