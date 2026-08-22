// Spectral verification of EngineSynth, plus audible WAV renders.
//
// The key assertion is that energy lands on the engine's firing orders: a
// four-stroke's dominant order is cylinders/2, so a V8 at 3000 rpm must peak at
// 4 x 50 Hz = 200 Hz. If that holds, the crank-angle model is firing correctly.
#include "EngineSynth.h"
#include "EngineSoundProfile.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace raceman;

static int g_failures = 0;
static const int kRate = 48000;

static void Check(bool ok, const std::string& label, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                detail.empty() ? "" : "  -> ", detail.c_str());
    if (!ok) ++g_failures;
}

// Magnitude of one frequency bin (Goertzel-style direct DFT).
static double BinMagnitude(const std::vector<float>& x, double freq, int rate) {
    const double w = 2.0 * 3.14159265358979323846 * freq / rate;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < x.size(); ++n) {
        re += x[n] * std::cos(w * n);
        im += x[n] * std::sin(w * n);
    }
    return std::sqrt(re * re + im * im) / x.size();
}

static EngineSoundProfile MakeProfile(EngineLayoutPreset preset, float redline) {
    EngineSoundProfile p = EngineSoundProfileLoader::makeDefault();
    p.cylinders = MakeEngineLayout(preset);
    p.orders = MakeDefaultOrderBank(static_cast<int>(p.cylinders.size()), redline);
    return p;
}

static std::vector<float> RenderSteady(const EngineSoundProfile& profile, float rpm,
                                       float load, float seconds, float redline = 7000.0f) {
    EngineSynth synth;
    synth.SetProfile(EngineSynthBaked::Bake(profile, 900.0f, redline, kRate));

    EngineSynthParams params;
    params.rpm = rpm;
    params.idleRpm = 900.0f;
    params.redlineRpm = redline;
    params.load = load;
    params.throttle = load;
    synth.SetParams(params);

    const int total = static_cast<int>(seconds * kRate);
    std::vector<float> out(total, 0.0f);
    const int block = 480;
    for (int i = 0; i < total; i += block) {
        synth.Render(out.data() + i, std::min(block, total - i), kRate);
    }
    // Drop the first 0.25 s so waveguides have settled.
    const int skip = kRate / 4;
    return std::vector<float>(out.begin() + skip, out.end());
}

// ---------------------------------------------------------------------------

static void TestFiringOrderSpectrum() {
    std::printf("\n1. Energy lands on the firing order (crank-angle model is correct)\n");
    struct Case { EngineLayoutPreset preset; const char* name; int cylinders; };
    const Case cases[] = {
        {EngineLayoutPreset::I4, "I4", 4},
        {EngineLayoutPreset::I6, "I6", 6},
        {EngineLayoutPreset::V8CrossPlane, "V8", 8},
    };
    const float rpm = 3000.0f;
    const double crankHz = rpm / 60.0; // 50 Hz

    for (const Case& c : cases) {
        const std::vector<float> x = RenderSteady(MakeProfile(c.preset, 7000.0f), rpm, 1.0f, 1.25f);
        const double dominantOrder = c.cylinders / 2.0;
        const double dominantHz = dominantOrder * crankHz;

        // Compare the dominant order against neighbouring non-order frequencies.
        const double atOrder = BinMagnitude(x, dominantHz, kRate);
        const double offA = BinMagnitude(x, dominantHz * 1.31, kRate);
        const double offB = BinMagnitude(x, dominantHz * 0.71, kRate);
        const double offPeak = std::max(offA, offB);

        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "%s: order %.1f = %.0f Hz, mag %.5f vs off-order %.5f (%.1fx)",
                      c.name, dominantOrder, dominantHz, atOrder, offPeak,
                      offPeak > 0 ? atOrder / offPeak : 0.0);
        Check(atOrder > offPeak * 2.0, "dominant firing order dominates the spectrum", detail);
    }
}

static void TestRpmTracksFrequency() {
    std::printf("\n2. Firing frequency tracks RPM linearly\n");
    const EngineSoundProfile p = MakeProfile(EngineLayoutPreset::I4, 7000.0f);
    // I4 dominant order is 2, so firing Hz = rpm/60 * 2.
    const float rpms[] = {1500.0f, 3000.0f, 6000.0f};
    bool allOk = true;
    std::string detail;
    for (float rpm : rpms) {
        const std::vector<float> x = RenderSteady(p, rpm, 1.0f, 1.25f);
        const double expected = rpm / 60.0 * 2.0;
        const double atExpected = BinMagnitude(x, expected, kRate);
        const double atHalf = BinMagnitude(x, expected * 0.61, kRate);
        const bool ok = atExpected > atHalf * 1.8;
        allOk = allOk && ok;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.0frpm->%.0fHz(%.4f) ", rpm, expected, atExpected);
        detail += buf;
    }
    Check(allOk, "peak moves with RPM", detail);
}

static void TestCrossPlaneVsFlatPlane() {
    std::printf("\n3. Cross-plane and flat-plane V8 differ (the burble mechanism)\n");
    // Identical global firing sequence; only the bank split differs. If the
    // per-bank exhaust runners work, the spectra must diverge at half orders.
    const float rpm = 3000.0f;
    const double crankHz = rpm / 60.0;
    // Roar is disabled here on purpose: it is broadband and identical for both
    // engines, so it lands in the very half-order bins that carry the bank
    // signature and dilutes the comparison. Worth remembering when tuning -
    // too much roar audibly washes out the cross-plane burble.
    EngineSoundProfile crossProfile = MakeProfile(EngineLayoutPreset::V8CrossPlane, 7000.0f);
    EngineSoundProfile flatProfile  = MakeProfile(EngineLayoutPreset::V8FlatPlane,  7000.0f);
    crossProfile.roar.gain = 0.0f;
    flatProfile.roar.gain  = 0.0f;
    const std::vector<float> cross = RenderSteady(crossProfile, rpm, 1.0f, 1.5f);
    const std::vector<float> flat  = RenderSteady(flatProfile,  rpm, 1.0f, 1.5f);

    // Half orders (0.5, 1.5, 2.5, 3.5) carry the uneven-bank signature.
    double crossHalf = 0.0, flatHalf = 0.0;
    for (double order : {0.5, 1.5, 2.5, 3.5}) {
        crossHalf += BinMagnitude(cross, order * crankHz, kRate);
        flatHalf  += BinMagnitude(flat,  order * crankHz, kRate);
    }
    char detail[160];
    std::snprintf(detail, sizeof(detail), "cross-plane half-order energy %.5f vs flat-plane %.5f (%.2fx)",
                  crossHalf, flatHalf, flatHalf > 0 ? crossHalf / flatHalf : 0.0);
    Check(std::fabs(crossHalf - flatHalf) / std::max(1e-9, std::max(crossHalf, flatHalf)) > 0.15,
          "bank split changes the spectrum", detail);
}

static void TestNoAliasingOrBlowUp() {
    std::printf("\n4. Numerical safety across the whole range\n");
    const EngineSoundProfile p = MakeProfile(EngineLayoutPreset::V12, 9000.0f);

    EngineSynth synth;
    synth.SetProfile(EngineSynthBaked::Bake(p, 900.0f, 9000.0f, kRate));

    std::vector<float> out(kRate * 3, 0.0f);
    const int block = 480;
    bool finite = true, inRange = true;
    float peak = 0.0f;
    for (int i = 0; i < static_cast<int>(out.size()); i += block) {
        const float t = static_cast<float>(i) / static_cast<float>(out.size());
        EngineSynthParams params;
        params.rpm = 900.0f + t * (9000.0f - 900.0f);
        params.idleRpm = 900.0f; params.redlineRpm = 9000.0f;
        params.load = 1.0f; params.throttle = 1.0f;
        synth.SetParams(params);
        synth.Render(out.data() + i, std::min(block, static_cast<int>(out.size()) - i), kRate);
    }
    for (float v : out) {
        if (!std::isfinite(v)) finite = false;
        if (v < -1.0f || v > 1.0f) inRange = false;
        peak = std::max(peak, std::fabs(v));
    }
    Check(finite, "no NaN or Inf across an idle-to-redline sweep");
    Check(inRange, "output stays within [-1, 1]");
    Check(peak > 0.02f, "output is not silent", "peak=" + std::to_string(peak));
}

static void TestLoadChangesTimbre() {
    std::printf("\n5. Load changes timbre (on-load vs off-load)\n");
    const EngineSoundProfile p = MakeProfile(EngineLayoutPreset::I4, 7000.0f);
    const std::vector<float> onLoad  = RenderSteady(p, 4000.0f, 1.0f, 1.0f);
    const std::vector<float> offLoad = RenderSteady(p, 4000.0f, 0.0f, 1.0f);

    double onRms = 0.0, offRms = 0.0;
    for (float v : onLoad)  onRms  += v * v;
    for (float v : offLoad) offRms += v * v;
    onRms  = std::sqrt(onRms  / onLoad.size());
    offRms = std::sqrt(offRms / offLoad.size());

    char detail[128];
    std::snprintf(detail, sizeof(detail), "on-load RMS %.5f vs off-load %.5f", onRms, offRms);
    Check(onRms > offRms * 1.4, "pulling is louder and fuller than coasting", detail);
}

static void TestIgnitionCutSilencesCombustion() {
    std::printf("\n6. Ignition cut (limiter / shift) actually cuts combustion\n");
    const EngineSoundProfile p = MakeProfile(EngineLayoutPreset::I4, 7000.0f);

    EngineSynth synth;
    synth.SetProfile(EngineSynthBaked::Bake(p, 900.0f, 7000.0f, kRate));
    EngineSynthParams params;
    params.rpm = 5000.0f; params.idleRpm = 900.0f; params.redlineRpm = 7000.0f;
    params.load = 1.0f; params.throttle = 1.0f;

    std::vector<float> normal(kRate / 2), cut(kRate / 2);
    synth.SetParams(params);
    for (int i = 0; i < static_cast<int>(normal.size()); i += 480)
        synth.Render(normal.data() + i, std::min(480, static_cast<int>(normal.size()) - i), kRate);

    params.ignitionCut = true;
    synth.SetParams(params);
    for (int i = 0; i < static_cast<int>(cut.size()); i += 480)
        synth.Render(cut.data() + i, std::min(480, static_cast<int>(cut.size()) - i), kRate);

    double nRms = 0.0, cRms = 0.0;
    for (float v : normal) nRms += v * v;
    // Measure the tail, after the waveguides have rung out.
    for (size_t i = cut.size() / 2; i < cut.size(); ++i) cRms += cut[i] * cut[i];
    nRms = std::sqrt(nRms / normal.size());
    cRms = std::sqrt(cRms / (cut.size() / 2));

    char detail[128];
    std::snprintf(detail, sizeof(detail), "firing RMS %.5f vs cut RMS %.5f", nRms, cRms);
    Check(cRms < nRms * 0.25, "ignition cut drops the level sharply", detail);
}

static void TestRoarAddsBroadbandEnergy() {
    std::printf("\n7. Roar bed adds broadband energy between the comb peaks\n");
    // A pure waveguide gives discrete peaks with near-silent gaps - the "tin
    // can". The roar bed should fill those gaps with pulsing noise.
    EngineSoundProfile quiet = MakeProfile(EngineLayoutPreset::V8CrossPlane, 7000.0f);
    quiet.roar.gain = 0.0f;
    EngineSoundProfile loud = quiet;
    loud.roar.gain = 1.0f;

    const float rpm = 3000.0f;
    const double crankHz = rpm / 60.0;
    const std::vector<float> dry = RenderSteady(quiet, rpm, 1.0f, 1.25f);
    const std::vector<float> wet = RenderSteady(loud, rpm, 1.0f, 1.25f);

    // Sample clearly between harmonics of the 200 Hz firing order.
    double dryGap = 0.0, wetGap = 0.0;
    for (double f : {130.0, 170.0, 230.0, 270.0, 310.0}) {
        dryGap += BinMagnitude(dry, f, kRate);
        wetGap += BinMagnitude(wet, f, kRate);
    }
    char detail[160];
    std::snprintf(detail, sizeof(detail), "between-harmonic energy %.5f -> %.5f (%.1fx)",
                  dryGap, wetGap, dryGap > 0 ? wetGap / dryGap : 0.0);
    Check(wetGap > dryGap * 1.5, "roar fills the gaps between comb peaks", detail);

    // Modulation depth is about texture rather than level. The RNG is
    // deterministic, so comparing the two renders sample-wise shows directly
    // whether gating changes the signal at all.
    EngineSoundProfile flat = loud;   flat.roar.modDepth = 0.0f;   flat.roar.gain = 2.0f;
    EngineSoundProfile pulsed = loud; pulsed.roar.modDepth = 1.0f; pulsed.roar.gain = 2.0f;
    const std::vector<float> flatOut   = RenderSteady(flat, rpm, 1.0f, 1.0f);
    const std::vector<float> pulsedOut = RenderSteady(pulsed, rpm, 1.0f, 1.0f);

    double diffSq = 0.0, refSq = 0.0;
    const size_t n = std::min(flatOut.size(), pulsedOut.size());
    for (size_t i = 0; i < n; ++i) {
        const double d = double(flatOut[i]) - pulsedOut[i];
        diffSq += d * d;
        refSq  += double(flatOut[i]) * flatOut[i];
    }
    const double relative = (refSq > 1e-12) ? std::sqrt(diffSq / refSq) : 0.0;
    std::snprintf(detail, sizeof(detail), "gated vs ungated differ by %.1f%% RMS", relative * 100.0);
    Check(relative > 0.05, "modulation depth measurably gates the roar", detail);
}

static void TestReverbAddsTail() {
    std::printf("\n8. Reverb puts the engine in a space\n");
    EngineSoundProfile dryP = MakeProfile(EngineLayoutPreset::V8CrossPlane, 7000.0f);
    dryP.reverb.enabled = false;
    EngineSoundProfile wetP = dryP;
    wetP.reverb.enabled = true;
    wetP.reverb.earlyGain = 0.5f;
    wetP.reverb.tailGain = 0.35f;
    wetP.reverb.tailDecaySeconds = 0.8f;

    // Render a burst then silence, and measure what is still ringing after the
    // engine stops. A dry synth goes quiet almost immediately.
    auto renderDecay = [&](const EngineSoundProfile& profile) {
        EngineSynth synth;
        synth.SetProfile(EngineSynthBaked::Bake(profile, 900.0f, 7000.0f, kRate));
        EngineSynthParams params;
        params.rpm = 4000.0f; params.idleRpm = 900.0f; params.redlineRpm = 7000.0f;
        params.load = 1.0f; params.throttle = 1.0f;
        synth.SetParams(params);
        std::vector<float> out(kRate, 0.0f);
        for (int i = 0; i < kRate / 2; i += 480) synth.Render(out.data() + i, 480, kRate);
        // Cut ignition: only the space should still be audible.
        params.ignitionCut = true; params.load = 0.0f; params.throttle = 0.0f;
        synth.SetParams(params);
        for (int i = kRate / 2; i < kRate; i += 480) synth.Render(out.data() + i, 480, kRate);
        double tailSq = 0.0;
        const int tailStart = kRate / 2 + kRate / 100;   // 10 ms after the cut
        const int tailEnd   = kRate / 2 + kRate / 12;    // ~83 ms after
        for (int i = tailStart; i < tailEnd; ++i) tailSq += double(out[i]) * out[i];
        return std::sqrt(tailSq / std::max(1, tailEnd - tailStart));
    };

    const double dryTail = renderDecay(dryP);
    const double wetTail = renderDecay(wetP);
    char detail[160];
    std::snprintf(detail, sizeof(detail), "post-cut ring: dry %.5f vs wet %.5f (%.1fx)",
                  dryTail, wetTail, dryTail > 1e-9 ? wetTail / dryTail : 0.0);
    Check(wetTail > dryTail * 1.25, "reverb leaves a tail after the engine stops", detail);
}

static void TestAttackAddsTransient() {
    std::printf("\n9. Attack spike sharpens the bark\n");
    EngineSoundProfile soft = MakeProfile(EngineLayoutPreset::V8CrossPlane, 7000.0f);
    soft.combustionAttackGain = 0.0f;
    EngineSoundProfile hard = soft;
    hard.combustionAttackGain = 1.0f;

    // Crest factor of the final mix is a poor measure here: the waveguides and
    // reverb smear the spike. What the attack actually contributes is
    // high-frequency bite, so measure that directly.
    auto highEnergy = [](const std::vector<float>& x) {
        double sum = 0.0;
        for (double f : {1500.0, 2200.0, 3000.0, 4000.0}) sum += BinMagnitude(x, f, kRate);
        return sum;
    };
    const double softHigh = highEnergy(RenderSteady(soft, 3000.0f, 1.0f, 1.0f));
    const double hardHigh = highEnergy(RenderSteady(hard, 3000.0f, 1.0f, 1.0f));
    char detail[160];
    std::snprintf(detail, sizeof(detail), "high-frequency energy %.6f -> %.6f (%.1fx)",
                  softHigh, hardHigh, softHigh > 1e-12 ? hardHigh / softHigh : 0.0);
    Check(hardHigh > softHigh * 1.2, "attack adds high-frequency bite", detail);
}

// --- WAV output so the result can actually be listened to -------------------

static void WriteWav(const std::string& path, const std::vector<float>& samples, int rate) {
    std::vector<std::int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float v = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcm[i] = static_cast<std::int16_t>(v * 32767.0f);
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr) {
        std::printf("   could not write %s\n", path.c_str());
        return;
    }
    auto u32 = [&](std::uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](std::uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1);
    u32(static_cast<std::uint32_t>(rate)); u32(static_cast<std::uint32_t>(rate * 2)); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(dataBytes);
    fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
    std::printf("   wrote %s (%.1f s)\n", path.c_str(), samples.size() / static_cast<double>(rate));
}

static std::vector<float> RenderSweep(const EngineSoundProfile& profile, float redline, float seconds) {
    EngineSynth synth;
    synth.SetProfile(EngineSynthBaked::Bake(profile, 900.0f, redline, kRate));

    const int total = static_cast<int>(seconds * kRate);
    std::vector<float> out(total, 0.0f);
    const int block = 480;
    for (int i = 0; i < total; i += block) {
        const float t = static_cast<float>(i) / static_cast<float>(total);
        // Idle, pull to redline, lift, settle back to idle.
        float rpm, load;
        if (t < 0.15f)      { rpm = 900.0f; load = 0.0f; }
        else if (t < 0.70f) { const float k = (t - 0.15f) / 0.55f; rpm = 900.0f + k * (redline - 900.0f); load = 1.0f; }
        else if (t < 0.80f) { rpm = redline; load = 0.0f; }
        else                { const float k = (t - 0.80f) / 0.20f; rpm = redline - k * (redline - 900.0f); load = 0.0f; }

        EngineSynthParams params;
        params.rpm = rpm; params.idleRpm = 900.0f; params.redlineRpm = redline;
        params.load = load; params.throttle = load;
        synth.SetParams(params);
        synth.Render(out.data() + i, std::min(block, total - i), kRate);
    }
    return out;
}

static void RenderAudibleExamples(const std::string& outDir) {
    std::printf("\n7. Rendering audible examples\n");
    struct Case { EngineLayoutPreset preset; const char* file; float redline; };
    const Case cases[] = {
        {EngineLayoutPreset::V8CrossPlane, "v8_crossplane", 7000.0f},
        {EngineLayoutPreset::V8FlatPlane,  "v8_flatplane",  8500.0f},
        {EngineLayoutPreset::I4,           "i4",            7000.0f},
        {EngineLayoutPreset::I6,           "i6",            7000.0f},
        {EngineLayoutPreset::V12,          "v12",           9000.0f},
        {EngineLayoutPreset::VTwin90,      "vtwin90",       6000.0f},
    };
    for (const Case& c : cases) {
        EngineSoundProfile p = MakeProfile(c.preset, c.redline);
        WriteWav(outDir + "/" + c.file + "_sweep.wav", RenderSweep(p, c.redline, 6.0f), kRate);
    }
}

int main(int argc, char** argv) {
    std::printf("EngineSynth spectral checks\n===========================\n");
    TestFiringOrderSpectrum();
    TestRpmTracksFrequency();
    TestCrossPlaneVsFlatPlane();
    TestNoAliasingOrBlowUp();
    TestLoadChangesTimbre();
    TestIgnitionCutSilencesCombustion();
    TestRoarAddsBroadbandEnergy();
    TestReverbAddsTail();
    TestAttackAddsTransient();
    RenderAudibleExamples(argc > 1 ? argv[1] : ".");
    std::printf("\n===========================\n");
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
