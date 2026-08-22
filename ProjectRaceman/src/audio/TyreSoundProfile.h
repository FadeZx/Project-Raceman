#pragma once

#include <string>
#include <vector>

namespace raceman {

// -------------------------------------------------------------------------
// Per-surface tyre character.
//
// The vehicle sim already reports which surface every wheel is on, so a car
// putting two wheels over a kerb or onto grass can be heard doing it. That
// transition is one of the most informative sounds in a racing game and it
// costs almost nothing once the data is already there.
// -------------------------------------------------------------------------
struct TyreSurfaceSound {
    // Rolling noise: broadband, band-limited, scaled by speed and wheel load.
    float rollGain{1.00f};
    float rollLowHz{120.0f};
    float rollHighHz{2600.0f};
    // Grain gives gravel and dirt their crunch: amplitude modulation at a rate
    // proportional to road speed, as individual stones pass under the tread.
    float grainAmount{0.0f};
    float grainRateScale{1.0f};

    // How readily this surface squeals. Tarmac sings; grass and dirt just wash.
    float squealGain{1.00f};
    // Rumble strips are periodic, not random: a strong low tone whose rate
    // tracks speed.
    float rumbleGain{0.0f};
    float rumbleHz{55.0f};
};

struct TyreSoundProfile {
    std::string name{"default"};

    // --- rolling ---
    float rollMasterGain{0.55f};
    float rollSpeedRefMps{28.0f};   // speed at which rolling noise is at full level
    float rollLoadInfluence{0.55f}; // how much wheel load lifts the rolling noise

    // --- slip squeal ---
    // A tyre at the limit sings at a fairly fixed frequency set by tread block
    // resonance; it rises a little as slip increases but does not sweep freely.
    float squealMasterGain{0.75f};
    float squealBaseHz{760.0f};
    float squealRiseHz{420.0f};     // added at full slip
    float squealResonance{7.0f};
    float squealSlipThreshold{0.18f};  // below this a tyre is simply gripping
    float squealSlipFull{0.85f};
    // A squeal that switches on instantly sounds like a trigger, which is
    // exactly what the old one-shot did wrong.
    float squealAttackSeconds{0.06f};
    float squealReleaseSeconds{0.22f};

    // --- impacts ---
    float impactGain{0.70f};        // kerb strikes and landings from suspension spikes
    float impactThreshold{0.35f};   // suspension travel rate that counts as a hit

    // --- mix ---
    float masterVolume{1.0f};
    float minDistance{6.0f};
    float maxDistance{120.0f};
    float spatialBlend{1.0f};

    // Indexed by TrackSurfaceType.
    std::vector<TyreSurfaceSound> surfaces;
};

class TyreSoundProfileLoader {
public:
    static TyreSoundProfile loadFromFile(const std::string& path);
    static bool saveToFile(const std::string& path, const TyreSoundProfile& profile,
                           std::string* outError = nullptr);
    static TyreSoundProfile makeDefault();
};

} // namespace raceman
