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
    // A locked wheel is loud on every surface, but only tarmac makes it tonal.
    // Gravel ploughs, grass hisses - both need level without the singing.
    float lockGain{1.00f};
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

    // --- how slip is measured ---
    // Slip normalised against a fixed absolute speed is wrong at both ends: at
    // 200 km/h any real slide pegs it at full, and in a car park a genuine lock
    // barely registers. A tyre's behaviour depends on slip *ratio*, so the
    // reference scales with road speed and only falls back to a floor when
    // nearly stopped.
    float slipReferenceMps{3.5f};        // floor, so low-speed slides still read
    float slipReferenceFraction{0.20f};  // plus this fraction of road speed

    // --- lateral scrub squeal (the tyre singing at the limit) ---
    // A tyre at the limit sings at a frequency set by tread block resonance; it
    // rises a little as slip increases but does not sweep freely.
    float squealMasterGain{0.75f};
    float squealBaseHz{760.0f};
    float squealRiseHz{420.0f};     // added at full slip
    float squealResonance{7.0f};
    float squealSlipThreshold{0.18f};  // below this a tyre is simply gripping
    float squealSlipFull{0.85f};
    // A heavily loaded tyre has a longer contact patch and sings lower. This is
    // why the outside front drops in pitch as the car takes a set.
    float squealLoadPitchInfluence{0.22f};
    // A squeal that switches on instantly sounds like a trigger, which is
    // exactly what the old one-shot did wrong.
    float squealAttackSeconds{0.06f};
    float squealReleaseSeconds{0.22f};

    // --- lock-up (brakes beat grip; the wheel stops turning) ---
    // Not a squeal. A locked tyre is a flat-spotting graunch: lower, much
    // broader band, and its level tracks ROAD speed rather than slip, because
    // a fully locked wheel is already at maximum slip by definition. Pitch
    // deliberately does not rise with slip - that is the tell that separates a
    // lock-up from a slide by ear.
    float lockGain{0.90f};
    float lockCentreHz{330.0f};
    float lockBandwidth{0.60f};     // 0 = narrow tone, 1 = broadband roar
    float lockFullSpeedMps{16.0f};  // road speed at which lock-up is at full level
    // Stick-slip judder: the shudder that makes a lock-up sound like rubber
    // tearing rather than white noise. Rate rises with speed.
    float lockJudderHz{24.0f};
    float lockJudderDepth{0.45f};
    // A lock happens the instant the brake bites, and releases just as fast.
    float lockAttackSeconds{0.02f};
    float lockReleaseSeconds{0.09f};

    // --- wheelspin (drive torque beat grip) ---
    // Higher and thinner than a scrub, and it genuinely does climb, because the
    // contact patch speed is climbing with the wheel.
    float spinGain{0.80f};
    float spinBaseHz{980.0f};
    float spinRiseHz{760.0f};

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
