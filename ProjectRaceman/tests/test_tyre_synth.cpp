// Behaviour checks for TyreSynth. The headline claims are that surfaces are
// audibly distinct and that squeal is CONTINUOUS with slip rather than a
// threshold trigger, which is what the old one-shot got wrong.
#include "TyreSynth.h"
#include "TyreSoundProfile.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
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

static double BinMagnitude(const std::vector<float>& x, double freq, int rate) {
    const double w = 2.0 * 3.14159265358979323846 * freq / rate;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < x.size(); ++n) {
        re += x[n] * std::cos(w * n);
        im += x[n] * std::sin(w * n);
    }
    return std::sqrt(re * re + im * im) / x.size();
}

static double Rms(const std::vector<float>& x) {
    double s = 0.0;
    for (float v : x) s += double(v) * v;
    return std::sqrt(s / std::max<size_t>(1, x.size()));
}

static std::vector<float> Render(const TyreSoundProfile& profile, float speed, float slip,
                                 int surface, float seconds = 0.6f, float load = 0.5f) {
    TyreSynth synth;
    synth.SetProfile(std::make_shared<TyreSoundProfile>(profile));

    TyreSynthParams params;
    params.speedMps = speed;
    params.wheelCount = 4;
    for (int w = 0; w < 4; ++w) {
        params.wheels[w].grounded = true;
        params.wheels[w].load = load;
        params.wheels[w].slip = slip;
        params.wheels[w].surfaceA = surface;
        params.wheels[w].surfaceB = surface;
    }
    synth.SetParams(params);

    const int total = static_cast<int>(seconds * kRate);
    std::vector<float> out(total, 0.0f);
    for (int i = 0; i < total; i += 480) {
        synth.Render(out.data() + i, std::min(480, total - i), kRate);
    }
    // Skip the settling period so envelopes have arrived.
    return std::vector<float>(out.begin() + kRate / 4, out.end());
}

// ---------------------------------------------------------------------------

static void TestRollingScalesWithSpeed() {
    std::printf("\n1. Rolling noise scales with road speed\n");
    const TyreSoundProfile p = TyreSoundProfileLoader::makeDefault();
    const double slow = Rms(Render(p, 5.0f, 0.0f, 0));
    const double fast = Rms(Render(p, 40.0f, 0.0f, 0));
    char detail[128];
    std::snprintf(detail, sizeof(detail), "5 m/s RMS %.5f vs 40 m/s %.5f", slow, fast);
    Check(fast > slow * 2.0, "faster means louder rolling", detail);

    const double stopped = Rms(Render(p, 0.0f, 0.0f, 0));
    Check(stopped < slow * 0.5, "a stationary car has no rolling noise",
          "stopped RMS " + std::to_string(stopped));
}

static void TestSurfacesAreDistinct() {
    std::printf("\n2. Surfaces are audibly different\n");
    const TyreSoundProfile p = TyreSoundProfileLoader::makeDefault();
    // Asphalt = 0, Dirt = 1, Grass = 2, Curb = 3
    const std::vector<float> asphalt = Render(p, 25.0f, 0.0f, 0);
    const std::vector<float> grass   = Render(p, 25.0f, 0.0f, 2);
    const std::vector<float> curb    = Render(p, 25.0f, 0.0f, 3);

    // Grass is duller: less energy up high than asphalt.
    const double asphaltHigh = BinMagnitude(asphalt, 2200.0, kRate);
    const double grassHigh   = BinMagnitude(grass, 2200.0, kRate);
    char detail[160];
    std::snprintf(detail, sizeof(detail), "2.2 kHz energy asphalt %.6f vs grass %.6f",
                  asphaltHigh, grassHigh);
    Check(asphaltHigh > grassHigh * 1.5, "grass is duller than asphalt", detail);

    // A kerb adds strong low-frequency rumble that tarmac does not have.
    const double asphaltLow = BinMagnitude(asphalt, 70.0, kRate);
    const double curbLow    = BinMagnitude(curb, 70.0, kRate);
    std::snprintf(detail, sizeof(detail), "70 Hz energy asphalt %.6f vs kerb %.6f",
                  asphaltLow, curbLow);
    Check(curbLow > asphaltLow * 2.0, "kerbs rumble", detail);
}

static void TestSquealIsContinuous() {
    std::printf("\n3. Squeal is continuous with slip, not a threshold trigger\n");
    const TyreSoundProfile p = TyreSoundProfileLoader::makeDefault();
    // Sample the squeal band across a slip ramp. A trigger would jump; a real
    // tyre rises progressively.
    const double slips[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    double previous = -1.0;
    bool monotonic = true;
    std::string trace;
    for (double slip : slips) {
        const std::vector<float> x = Render(p, 25.0f, static_cast<float>(slip), 0);
        const double band = BinMagnitude(x, p.squealBaseHz + p.squealRiseHz * slip, kRate);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.2f:%.5f ", slip, band);
        trace += buf;
        if (previous >= 0.0 && band < previous * 0.9) monotonic = false;
        previous = band;
    }
    Check(monotonic, "squeal rises progressively with slip", trace);

    const double gripping = Rms(Render(p, 25.0f, 0.05f, 0));
    const double sliding  = Rms(Render(p, 25.0f, 1.0f, 0));
    char detail[128];
    std::snprintf(detail, sizeof(detail), "gripping RMS %.5f vs sliding %.5f", gripping, sliding);
    Check(sliding > gripping * 1.5, "a sliding tyre is louder than a gripping one", detail);
}

static void TestLooseSurfacesDoNotSing() {
    std::printf("\n4. Loose surfaces slide rather than squeal\n");
    const TyreSoundProfile p = TyreSoundProfileLoader::makeDefault();
    const double tarmacSqueal = BinMagnitude(Render(p, 25.0f, 1.0f, 0), p.squealBaseHz + p.squealRiseHz, kRate);
    const double grassSqueal  = BinMagnitude(Render(p, 25.0f, 1.0f, 2), p.squealBaseHz + p.squealRiseHz, kRate);
    char detail[160];
    std::snprintf(detail, sizeof(detail), "squeal band tarmac %.6f vs grass %.6f",
                  tarmacSqueal, grassSqueal);
    Check(tarmacSqueal > grassSqueal * 3.0, "grass does not sing at the limit", detail);
}

static void TestNumericalSafety() {
    std::printf("\n5. Numerical safety\n");
    const TyreSoundProfile p = TyreSoundProfileLoader::makeDefault();
    bool finite = true, inRange = true;
    for (int surface = 0; surface < 6; ++surface) {
        for (float speed : {0.0f, 12.0f, 45.0f, 90.0f}) {
            const std::vector<float> x = Render(p, speed, 0.6f, surface, 0.35f);
            for (float v : x) {
                if (!std::isfinite(v)) finite = false;
                if (v < -1.0f || v > 1.0f) inRange = false;
            }
        }
    }
    Check(finite, "no NaN or Inf across every surface and speed");
    Check(inRange, "output stays within [-1, 1]");
}

// --- audible renders --------------------------------------------------------

static void WriteWav(const std::string& path, const std::vector<float>& samples, int rate) {
    std::vector<std::int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float v = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcm[i] = static_cast<std::int16_t>(v * 32767.0f);
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr) return;
    auto u32 = [&](std::uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](std::uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1);
    u32((std::uint32_t)rate); u32((std::uint32_t)(rate * 2)); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(dataBytes);
    fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
    std::printf("   wrote %s\n", path.c_str());
}

static void RenderExamples(const std::string& outDir) {
    std::printf("\n6. Rendering audible examples\n");
    const TyreSoundProfile p = TyreSoundProfileLoader::makeDefault();

    // A lap-like pass: accelerate on tarmac, slide, clip a kerb, run wide onto grass.
    TyreSynth synth;
    synth.SetProfile(std::make_shared<TyreSoundProfile>(p));
    const int total = kRate * 8;
    std::vector<float> out(total, 0.0f);
    for (int i = 0; i < total; i += 480) {
        const float t = static_cast<float>(i) / total;
        TyreSynthParams params;
        params.wheelCount = 4;
        float speed, slip; int surface;
        if (t < 0.25f)      { speed = 10.0f + t * 4.0f * 25.0f; slip = 0.0f;  surface = 0; }
        else if (t < 0.45f) { speed = 35.0f; slip = (t - 0.25f) / 0.20f;      surface = 0; }
        else if (t < 0.60f) { speed = 32.0f; slip = 0.7f;                      surface = 3; }
        else if (t < 0.80f) { speed = 28.0f; slip = 0.9f;                      surface = 2; }
        else                { speed = 20.0f; slip = 0.1f;                      surface = 0; }
        for (int w = 0; w < 4; ++w) {
            params.wheels[w].grounded = true;
            params.wheels[w].load = 0.5f;
            params.wheels[w].slip = slip;
            params.wheels[w].surfaceA = surface;
            params.wheels[w].surfaceB = surface;
        }
        params.speedMps = speed;
        synth.SetParams(params);
        synth.Render(out.data() + i, std::min(480, total - i), kRate);
    }
    WriteWav(outDir + "/tyres_lap.wav", out, kRate);

    const char* names[4] = {"asphalt", "dirt", "grass", "curb"};
    for (int s = 0; s < 4; ++s) {
        WriteWav(outDir + "/tyres_" + names[s] + ".wav", Render(p, 28.0f, 0.35f, s, 3.0f), kRate);
    }
}

int main(int argc, char** argv) {
    std::printf("TyreSynth behaviour checks\n==========================\n");
    TestRollingScalesWithSpeed();
    TestSurfacesAreDistinct();
    TestSquealIsContinuous();
    TestLooseSurfacesDoNotSing();
    TestNumericalSafety();
    RenderExamples(argc > 1 ? argv[1] : ".");
    std::printf("\n==========================\n");
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
