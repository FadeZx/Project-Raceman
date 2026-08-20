#include "EngineSynthGenerator.h"

#include <cmath>

namespace raceman {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
}

void EngineSynthTestToneGenerator::Render(float* output, int frameCount, int sampleRate) {
    const double step = kTwoPi * static_cast<double>(frequencyHz_.load(std::memory_order_relaxed))
                      / static_cast<double>(sampleRate);
    for (int i = 0; i < frameCount; ++i) {
        output[i] = static_cast<float>(std::sin(phase_)) * amplitude_;
        phase_ += step;
        if (phase_ > kTwoPi) {
            phase_ -= kTwoPi;
        }
    }
    framesRendered_.fetch_add(frameCount, std::memory_order_relaxed);
}

} // namespace raceman
