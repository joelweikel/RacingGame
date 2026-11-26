#include "audio.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace {

constexpr float tau = 6.283185307f;
constexpr float sampleRate = 48000.0f;

} // namespace

struct GameAudio::Playback {
    ma_device device{};
    bool open = false;
    std::atomic<float> targetRpm{idleRpm}, targetLoad{0.0f}, targetSlip{0.0f}, targetGain{0.0f};
    std::atomic<bool> gale{false};
    float rpm = idleRpm, load = 0.0f, slip = 0.0f, gain = 0.0f;
    float cycle = 0.0f, exhaust = 0.0f, tirePhase = 0.0f, noiseSlow = 0.0f, noiseFast = 0.0f;
    std::uint32_t noiseState = 591762u;

    Playback()
    {
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = static_cast<ma_uint32>(sampleRate);
        config.periodSizeInFrames = 512;
        config.pUserData = this;
        config.dataCallback = [](ma_device* device, void* output, const void*, ma_uint32 frames) {
            static_cast<Playback*>(device->pUserData)->render(static_cast<float*>(output), frames);
        };
        if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS) return;
        open = ma_device_start(&device) == MA_SUCCESS;
        if (!open) ma_device_uninit(&device);
    }

    ~Playback()
    {
        if (open) ma_device_uninit(&device);
    }

    void render(float* output, ma_uint32 frames)
    {
        const float revs = targetRpm.load(std::memory_order_relaxed);
        const float throttle = targetLoad.load(std::memory_order_relaxed);
        const float sliding = targetSlip.load(std::memory_order_relaxed);
        const float volume = targetGain.load(std::memory_order_relaxed);
        const bool flatPlane = gale.load(std::memory_order_relaxed);
        for (ma_uint32 sample = 0; sample < frames; ++sample) {
            rpm += (revs - rpm) * 0.0014f;
            load += (throttle - load) * 0.0007f;
            slip += (sliding - slip) * 0.0008f;
            gain += (volume - gain) * 0.0008f;
            cycle += rpm / (120.0f * sampleRate);
            if (cycle >= 1.0f) cycle -= 1.0f;
            const float crank = cycle * tau;
            const float idleCam = 1.0f - std::clamp((rpm - idleRpm) / 2400.0f, 0.0f, 1.0f);
            float wave;
            if (flatPlane) {
                const float firing = crank * 8.0f;
                const float howl = std::clamp((rpm - 3200.0f) / 5100.0f, 0.0f, 1.0f);
                wave = 0.14f * std::sin(crank * 4.0f) + 0.48f * std::sin(firing) +
                       (0.30f + howl * 0.07f) * std::sin(firing * 2.0f - 0.5f) +
                       (0.16f + howl * 0.08f) * std::sin(firing * 3.0f - 1.2f) +
                       0.10f * howl * std::sin(firing * 5.0f);
            } else {
                const float firing = crank * 10.0f + 0.42f * std::sin(crank * 5.0f);
                const float lope = 1.0f - idleCam * (0.23f + 0.22f * std::sin(crank * 2.0f));
                wave = lope * (0.40f * std::sin(crank * 5.0f) + 0.40f * std::sin(firing) +
                               0.21f * std::sin(firing * 2.0f + 0.8f) +
                               0.10f * load * std::sin(firing * 3.0f)) + 0.16f * idleCam * std::sin(crank * 2.0f);
            }
            wave *= 1.15f + 0.75f * load;
            wave /= 1.0f + std::abs(wave) * 0.68f;
            exhaust += (wave - exhaust) * (flatPlane ? 0.31f : (0.13f + 0.10f * load));

            noiseState = noiseState * 1664525u + 1013904223u;
            const float noise = static_cast<float>(noiseState >> 8) / 8388608.0f - 1.0f;
            noiseFast += (noise - noiseFast) * 0.28f;
            noiseSlow += (noise - noiseSlow) * 0.09f;
            tirePhase += tau * (780.0f + 440.0f * slip + 145.0f * noiseFast) / sampleRate;
            if (tirePhase >= tau) tirePhase -= tau;
            const float tire = slip * (0.21f * (noiseFast - noiseSlow) +
                0.085f * std::sin(tirePhase) + 0.026f * std::sin(tirePhase * 2.0f));
            const float engine = exhaust * (0.20f + 0.48f * load);
            output[sample * 2] = gain * (engine + tire);
            output[sample * 2 + 1] = gain * (engine + tire * 0.91f);
        }
    }
};

GameAudio::GameAudio() : playback_(std::make_unique<Playback>()) {}
GameAudio::~GameAudio() = default;

void GameAudio::update(CarModel model, const CarState& car, const DriveInput& input, bool driving)
{
    float skid = 0.0f;
    for (const auto& wheel : car.wheels) skid = std::max(skid, wheel.slip);
    const float motion = std::clamp((glm::length(car.velocity) - 0.8f) / 5.0f, 0.0f, 1.0f);
    playback_->gale.store(model == CarModel::Gale, std::memory_order_relaxed);
    playback_->targetRpm.store(car.rpm, std::memory_order_relaxed);
    playback_->targetLoad.store(std::abs(input.throttle), std::memory_order_relaxed);
    playback_->targetSlip.store(driving ? skid * motion : 0.0f, std::memory_order_relaxed);
    playback_->targetGain.store(driving ? 0.68f : 0.0f, std::memory_order_relaxed);
}
