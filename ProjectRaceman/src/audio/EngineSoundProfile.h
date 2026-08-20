#pragma once

#include "VehicleSoundProfile.h"

#include <string>
#include <vector>

namespace raceman {

// -------------------------------------------------------------------------
// Curves — piecewise-linear over RPM, flat-clamped at both ends.
// Mirrors the shape and evaluation of physics::sampleTorqueCurve.
// -------------------------------------------------------------------------
struct EngineCurvePoint {
    float rpm{0.0f};
    float value{0.0f};
};

float SampleEngineCurve(const std::vector<EngineCurvePoint>& curve, float rpm, float fallback = 0.0f);

// -------------------------------------------------------------------------
// Cylinder — one combustion event per 720 degrees of crank rotation.
//
// The fire-angle table is what makes a V8 sound like a V8. Two engines can
// share an identical global firing sequence and still sound nothing alike if
// their cylinders are split across banks differently: a cross-plane V8 gives
// one bank the uneven 0/180/270/450 pattern that produces the burble, while a
// flat-plane V8 gives both banks an even 0/180/360/540 and screams instead.
// Each bank therefore gets its own exhaust runner before the collector.
// -------------------------------------------------------------------------
struct EngineCylinder {
    float fireAngleDeg{0.0f};   // 0..720 within the four-stroke cycle
    int   bankId{0};            // which exhaust runner this cylinder feeds
    float gain{1.0f};           // per-cylinder combustion strength
    float timingJitter{0.0f};   // degrees of random scatter, adds life at idle
};

// -------------------------------------------------------------------------
// Engine order — a sinusoid at a fixed multiple of crank frequency.
// Order 1 is one cycle per revolution. A four-stroke's dominant order is
// cylinders/2 (4 for a V8, 2 for an I4, 3 for an I6).
//
// Two gain curves per order, crossfaded by engine load. This is the procedural
// equivalent of the on-throttle / off-throttle sample layer pairs that
// sample-based racing games record separately.
// -------------------------------------------------------------------------
struct EngineOrder {
    float order{1.0f};
    std::vector<EngineCurvePoint> gainOnLoad;
    std::vector<EngineCurvePoint> gainOffLoad;
};

struct EngineExhaustSettings {
    float runnerLengthM{0.55f};
    float collectorLengthM{0.90f};
    float gasSpeedMs{450.0f};       // hot exhaust gas travels faster than ambient air
    float runnerReflection{0.55f};
    float runnerDamping{0.35f};
    int   mufflerStages{2};
    float mufflerLengthM{0.35f};
    float mufflerReflection{0.45f};
    float tailpipeBrightness{0.60f};
};

struct EngineIntakeSettings {
    float lengthM{0.35f};
    float airSpeedMs{343.0f};
    float reflection{0.45f};
    float damping{0.50f};
    float noise{0.40f};
    float pulseGain{0.50f};
};

struct EngineTurboSettings {
    bool  enabled{false};
    bool  supercharger{false};      // rigid RPM lock, no spool lag
    float whistleRatio{9.0f};       // whistle frequency = ratio * crank frequency
    float whistleGain{0.25f};
    float whistleResonance{6.0f};
    float blowOffGain{0.50f};
    float blowOffDecaySeconds{0.18f};
};

struct EngineDrivetrainSettings {
    float whineGain{0.0f};          // straight-cut gearbox tone
    float whineRatio{14.0f};
    float whineResonance{4.0f};
    float shiftCutDepth{0.85f};     // how far ignition drops during a shift
};

struct EngineOverrunSettings {
    bool  enabled{true};
    float minRpm{3000.0f};
    float density{0.35f};           // pop probability per firing event on overrun
    float gain{0.70f};
    float limiterPopGain{0.50f};
};

struct EngineNoiseSettings {
    float valvetrainGain{0.12f};
    float valvetrainFreq{3200.0f};
    float valvetrainQ{1.20f};
};

// -------------------------------------------------------------------------
// Body resonance and tone.
//
// Waveguides alone give resonances set by pipe length, which for short race
// headers all sit above 800 Hz - bright and thin. Real cars have low, FIXED
// resonances from the bodyshell, cabin and long exhaust. Those matter twice
// over: they add weight, and because they do NOT move with RPM the harmonics
// sweep through them, which is what stops a revving engine sounding like a
// siren.
// -------------------------------------------------------------------------
struct EngineBodySettings {
    float resonance1Hz{95.0f};      // chest-thump
    float resonance1Q{2.5f};
    float resonance1Gain{0.70f};
    float resonance2Hz{165.0f};     // upper body / cabin
    float resonance2Q{3.0f};
    float resonance2Gain{0.40f};
    float subGain{0.35f};           // low-passed firing energy, adds weight
    float subCutoffHz{90.0f};
    float toneTilt{0.30f};          // 0 = raw and bright, 1 = dark and distant
};

struct EngineMixSettings {
    float exhaustGain{1.00f};
    float intakeGain{0.60f};
    float blockGain{0.50f};
    float drive{0.20f};             // output soft-clip amount
    float masterVolume{1.00f};
};

// -------------------------------------------------------------------------
// Full profile — saved as   <name>.enginesound.json
//
// The RPM range is deliberately NOT stored here. It is read from the vehicle's
// arcadeHandling config at runtime so the two can never drift apart.
// -------------------------------------------------------------------------
struct EngineSoundProfile {
    std::string name{"default"};

    int   strokes{4};
    std::vector<EngineCylinder> cylinders;
    float idleInstability{0.020f};   // slow random walk on RPM; a dead-steady idle reads as synthetic
    float combustionVariance{0.080f};
    // Length of the combustion burst. A very short burst is a click, which the
    // pipes then ring on - that is the "tin can" sound. Longer, softer pulses
    // excite the low resonances instead of the metallic top end.
    float combustionDurationMs{3.0f};
    float combustionNoise{0.45f};    // 0 = pure thump, 1 = all hiss

    EngineExhaustSettings    exhaust;
    EngineIntakeSettings     intake;
    std::vector<EngineOrder> orders;
    EngineTurboSettings      turbo;
    EngineDrivetrainSettings drivetrain;
    EngineOverrunSettings    overrun;
    EngineNoiseSettings      noise;
    EngineBodySettings       body;
    EngineMixSettings        mix;

    float spatialBlend{1.0f};
    float minDistance{3.0f};
    float maxDistance{80.0f};

    // Discrete sample-based events kept from the old system. Backfire is now
    // synthesised through the exhaust waveguide and is no longer a trigger.
    std::vector<VehicleSoundTriggerEntry> triggerSounds;
};

// -------------------------------------------------------------------------
// Firing-order presets
// -------------------------------------------------------------------------
enum class EngineLayoutPreset {
    I3, I4, I5, I6,
    Flat4, Flat6,
    V6, V8CrossPlane, V8FlatPlane, V10, V12,
    VTwin90,
};

const char* EngineLayoutPresetName(EngineLayoutPreset preset);
std::vector<EngineCylinder> MakeEngineLayout(EngineLayoutPreset preset);

// Default order bank sized to the cylinder count, so a fresh profile already
// emphasises the correct dominant order instead of sounding generic.
std::vector<EngineOrder> MakeDefaultOrderBank(int cylinderCount, float redlineRpm);

// -------------------------------------------------------------------------
// Loader — mirrors VehicleSoundProfileLoader
// -------------------------------------------------------------------------
class EngineSoundProfileLoader {
public:
    static EngineSoundProfile loadFromFile(const std::string& path);
    static bool saveToFile(const std::string& path, const EngineSoundProfile& profile,
                           std::string* outError = nullptr);
    static EngineSoundProfile makeDefault();

    // One-way import of a legacy <name>.vehiclesound.json: keeps the trigger
    // clips and picks a layout preset, discarding the pitched engine layers.
    static EngineSoundProfile fromLegacyProfile(const VehicleSoundProfile& legacy,
                                                EngineLayoutPreset preset);
};

} // namespace raceman
