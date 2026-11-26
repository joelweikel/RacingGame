#include "engine_sound.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float tau = 6.283185307f;

struct Firing {
    float angle;
    unsigned runnerDelay;
    float strength;
};

// The banks alternate. The V10 has unequal 90/54 degree intervals.
constexpr std::array<Firing, 10> slimRider{{
    {0, 21, 1.04f}, {90, 47, 1.00f}, {144, 32, 0.86f}, {234, 43, 1.05f},
    {288, 46, 0.97f}, {378, 55, 0.89f}, {432, 28, 1.08f}, {522, 40, 0.91f},
    {576, 37, 0.94f}, {666, 29, 1.01f},
}};

constexpr std::array<Firing, 8> gale{{
    {0, 16, 1.02f}, {90, 22, 0.99f}, {180, 20, 0.96f}, {270, 17, 1.00f},
    {360, 19, 1.01f}, {450, 24, 0.98f}, {540, 22, 0.99f}, {630, 19, 1.04f},
}};

float pressure(float time, float rise)
{
    if (time < 0.0f) return 0.0f;
    const float x = time / rise;
    return x * std::exp(1.0f - x);
}

float ring(float time, float frequency, float decay)
{
    return std::sin(tau * time * frequency) * std::exp(-time / decay);
}

} // namespace

EngineSound::EngineSound()
{
    // Precompute the pressure wave and the pipe response for each bank,
    // from a soft closed-throttle pulse to sharp blowdown at full load.
    for (std::size_t engine = 0; engine < pulses_.size(); ++engine) {
        const bool flatPlane = engine != 0;
        for (std::size_t bank = 0; bank < 2; ++bank) {
            const float length = bank == 0 ? 1.0f : (flatPlane ? 1.035f : 0.918f);
            for (std::size_t open = 0; open < 2; ++open) {
                auto& pulse = pulses_[engine][bank][open];
                const float rise = open ? (flatPlane ? 0.00021f : 0.00030f) : 0.00068f;
                const float fundamental = (flatPlane ? 156.0f : 90.0f) * length;
                float sum = 0.0f, windowSum = 0.0f;
                for (std::size_t i = 0; i < pulse.size(); ++i) {
                    const float t = static_cast<float>(i) / gameAudioRate;
                    const float reflection = (flatPlane ? 0.00149f : 0.00222f) / length;
                    const float blowdown = pressure(t, rise) - 0.46f * pressure(t, rise * 2.7f) -
                        0.36f * pressure(t - reflection, rise * 1.1f) +
                        0.18f * pressure(t - 2.0f * reflection, rise * 1.5f) -
                        0.08f * pressure(t - 3.0f * reflection, rise * 1.6f);
                    const float pipe = 0.20f * ring(t, fundamental, 0.0063f) +
                        0.125f * ring(t, fundamental * 2.67f, 0.0040f) +
                        (open ? 0.19f : 0.075f) * ring(t, fundamental * 5.1f, 0.0024f) +
                        (open ? 0.135f : 0.035f) * ring(t, fundamental * 9.3f, 0.0017f);
                    const float window = std::sin(3.14159265f * i / (pulse.size() - 1));
                    const float tail = std::min(1.0f, (pulse.size() - 1 - i) / 144.0f);
                    pulse[i] = (blowdown * (flatPlane ? 0.86f : 0.64f) + pipe) * tail;
                    sum += pulse[i];
                    windowSum += window * window;
                }
                for (std::size_t i = 0; i < pulse.size(); ++i) {
                    const float window = std::sin(3.14159265f * i / (pulse.size() - 1));
                    pulse[i] -= (sum / windowSum) * window * window;
                }
            }
        }
    }
}

void EngineSound::select(CarModel model)
{
    if (model == model_) return;
    model_ = model;
    crank_ = 0.0f;
    nextCylinder_ = 0;
    exhaust_ = {};
    burst_ = intake_ = intakeBody_ = valvetrain_ = 0.0f;
}

void EngineSound::exhaustPulse(std::size_t bank, unsigned delay, float strength, float load, float late)
{
    auto& channel = exhaust_[bank];
    const auto& shapes = pulses_[model_ == CarModel::Gale ? 1 : 0][bank];
    const float relaxed = strength * (1.0f - load);
    const float open = strength * load;
    float previous = 0.0f;
    for (std::size_t i = 0; i < pulseLength; ++i) {
        const float pressure = relaxed * shapes[0][i] + open * shapes[1][i];
        channel[(sampleIndex_ + delay + i) & (delayLength - 1)] += pressure + (previous - pressure) * late;
        previous = pressure;
    }
}

EngineSound::Stereo EngineSound::sample(float rpm, float load, float cut, float noise)
{
    const bool flatPlane = model_ == CarModel::Gale;
    const auto* events = flatPlane ? gale.data() : slimRider.data();
    const auto count = flatPlane ? gale.size() : slimRider.size();
    const float idle = std::clamp((1850.0f - rpm) / 1000.0f, 0.0f, 1.0f) * (flatPlane ? 0.23f : 1.0f);
    idleDrift_ += (noise - idleDrift_) * 0.0013f;
    const float degrees = rpm * (6.0f / gameAudioRate) * (1.0f + 0.36f * idle * idleDrift_);
    if (crank_ >= 720.0f) {
        crank_ -= 720.0f;
        nextCylinder_ = 0;
    }
    if (nextCylinder_ < count && crank_ >= events[nextCylinder_].angle) {
        const Firing& event = events[nextCylinder_];
        const float lope = idle * (nextCylinder_ % 5 == 0 ? 0.32f : -0.055f);
        const float strength = (0.25f + load * 0.87f) * cut *
            (event.strength - lope + (0.052f + 0.105f * idle) * noise);
        const float late = 1.0f - std::clamp((crank_ - event.angle) / degrees, 0.0f, 1.0f);
        exhaustPulse(nextCylinder_ % 2, event.runnerDelay, strength, load, late);
        intake_ += strength * 0.32f;
        valvetrain_ += 0.059f * (1.0f + 0.12f * noise);
        ++nextCylinder_;
    }
    crank_ += degrees;
    const float left = exhaust_[0][sampleIndex_], right = exhaust_[1][sampleIndex_];
    exhaust_[0][sampleIndex_] = exhaust_[1][sampleIndex_] = 0.0f;
    sampleIndex_ = (sampleIndex_ + 1) & (delayLength - 1);

    turbulence_ += (noise - turbulence_) * 0.12f;
    intakeBody_ += (intake_ - intakeBody_) * (flatPlane ? 0.075f : 0.044f);
    intake_ *= 0.958f;
    valvetrain_ *= 0.76f;
    burst_ *= 0.981f;
    const float air = (intake_ - intakeBody_) * (0.14f + 0.42f * load) * (flatPlane ? 1.0f : 0.63f) +
        turbulence_ * (load * intakeBody_ * 0.42f + burst_ * 0.43f) +
        (noise - turbulence_) * valvetrain_ * 0.20f;

    const float cross = flatPlane ? 0.67f : 0.39f;
    return {left * 0.79f + right * cross + air, right * 0.79f + left * cross + air * 0.91f};
}

void EngineSound::afterfire(float strength)
{
    exhaustPulse(0, 10, strength, 1.0f, 0.0f);
    exhaustPulse(1, 49, strength * 0.73f, 1.0f, 0.0f);
    burst_ = strength;
}
