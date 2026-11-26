#pragma once

#include "car.hpp"

#include <array>
#include <cstddef>

inline constexpr int gameAudioRate = 48000;

class EngineSound {
public:
    using Stereo = std::array<float, 2>;

    EngineSound();
    void select(CarModel model);
    Stereo sample(float rpm, float load, float cut, float noise);
    void afterfire(float strength);

private:
    static constexpr std::size_t pulseLength = 1152;
    static constexpr std::size_t delayLength = 2048;
    using Pulse = std::array<float, pulseLength>;

    void exhaustPulse(std::size_t bank, unsigned delay, float strength, float load, float late);

    std::array<std::array<std::array<Pulse, 2>, 2>, 2> pulses_{};
    std::array<std::array<float, delayLength>, 2> exhaust_{};
    CarModel model_ = CarModel::SlimRider;
    std::size_t sampleIndex_ = 0, nextCylinder_ = 0;
    float crank_ = 0.0f;
    float idleDrift_ = 0.0f, turbulence_ = 0.0f;
    float intake_ = 0.0f, intakeBody_ = 0.0f, valvetrain_ = 0.0f;
    float burst_ = 0.0f;
};
