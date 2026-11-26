#include "audio.hpp"
#include "engine_sound.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_NO_VORBIS
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

float mix(float a, float b, float amount) { return a + (b - a) * amount; }

float smooth(float low, float high, float value)
{
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Loop {
    std::vector<float> waveform;
    float phase = 0.0f;

    void load(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        if (bytes.empty()) return;

        auto config = ma_decoder_config_init(ma_format_f32, 1, gameAudioRate);
        ma_uint64 frames = 0;
        void* pcm = nullptr;
        if (ma_decode_memory(bytes.data(), bytes.size(), &config, &frames, &pcm) != MA_SUCCESS) return;
        const auto freePcm = [](void* data) { ma_free(data, nullptr); };
        const std::unique_ptr<void, decltype(freePcm)> memory(pcm, freePcm);
        const auto* samples = static_cast<const float*>(pcm);
        if (frames >= 16) waveform.assign(samples, samples + frames);
    }

    float sample(float rate)
    {
        if (waveform.empty()) return 0.0f;
        const std::size_t i = static_cast<std::size_t>(phase);
        const std::size_t next = i + 1 < waveform.size() ? i + 1 : 0;
        const float result = mix(waveform[i], waveform[next], phase - static_cast<float>(i));
        phase += rate;
        if (phase >= waveform.size()) phase -= static_cast<float>(waveform.size());
        return result;
    }
};

constexpr std::array<float, 4> recordedRpm{2977.0f, 4438.0f, 6040.0f, 7252.0f};
constexpr std::array<const char*, 4> galeRecordings{
    "gale-2977.wav", "gale-4438.wav", "gale-6040.wav", "gale-7252.wav"
};

} // namespace

struct GameAudio::Playback {
    ma_device device{};
    bool open = false;
    std::atomic<float> targetRpm{idleRpm}, targetThrottle{0.0f}, targetSlip{0.0f}, targetSpeed{0.0f}, targetGain{0.0f};
    std::atomic<bool> gale{false}, shifting{false};

    EngineSound engine;
    std::array<Loop, galeRecordings.size()> recordings;
    Loop tires;
    bool hasRecordings = false;
    float rpm = idleRpm, load = 0.0f, cut = 1.0f, slip = 0.0f, speed = 0.0f, gain = 0.0f;
    float recordingBody = 0.0f, road = 0.0f, roadHigh = 0.0f;
    std::array<float, 2> dc{};
    float lastThrottle = 0.0f;
    bool lastShift = false;
    int pops = 0, untilPop = 0;
    std::uint32_t noiseState = 591762u;

    explicit Playback(const std::filesystem::path& assets)
    {
        for (std::size_t i = 0; i < recordings.size(); ++i) recordings[i].load(assets / galeRecordings[i]);
        hasRecordings = std::all_of(recordings.begin(), recordings.end(), [](const Loop& loop) {
            return !loop.waveform.empty();
        });
        tires.load(assets / "tire-squeal.wav");

        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = gameAudioRate;
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

    float random()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float>(noiseState >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }

    float recordedExhaust()
    {
        std::array<float, recordedRpm.size()> samples{};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const float rate = std::clamp(rpm / recordedRpm[i], 0.50f, 2.0f);
            samples[i] = recordings[i].sample(rate);
        }

        std::size_t lo = 0;
        while (lo + 2 < recordedRpm.size() && rpm > recordedRpm[lo + 1]) ++lo;
        const float blend = smooth(recordedRpm[lo], recordedRpm[lo + 1], rpm);
        return mix(samples[lo], samples[lo + 1], blend);
    }

    void render(float* output, ma_uint32 frames)
    {
        const float revs = targetRpm.load(std::memory_order_relaxed);
        const float throttle = targetThrottle.load(std::memory_order_relaxed);
        const float sliding = targetSlip.load(std::memory_order_relaxed);
        const float velocity = targetSpeed.load(std::memory_order_relaxed);
        const float volume = targetGain.load(std::memory_order_relaxed);
        const bool flatPlane = gale.load(std::memory_order_relaxed);
        const bool shift = shifting.load(std::memory_order_relaxed);
        engine.select(flatPlane ? CarModel::Gale : CarModel::SlimRider);
        if (volume == 0.0f) {
            pops = 0;
        } else if (revs > 2900.0f && ((lastThrottle > 0.62f && throttle < 0.14f) ||
                                     (shift && !lastShift && throttle > 0.55f))) {
            pops = shift ? 1 : 2;
            untilPop = shift ? 460 : 1080;
        }
        lastThrottle = throttle;
        lastShift = shift;

        for (ma_uint32 frame = 0; frame < frames; ++frame) {
            rpm += (revs - rpm) * 0.00145f;
            const float exhaustLoad = throttle * (shift ? 0.18f : 1.0f);
            load += (exhaustLoad - load) * (exhaustLoad > load ? 0.0010f : 0.0019f);
            cut += ((shift ? 0.43f : 1.0f) - cut) * 0.0022f;
            slip += (sliding - slip) * (sliding > slip ? 0.0023f : 0.0010f);
            speed += (velocity - speed) * 0.0006f;
            gain += (volume - gain) * 0.0011f;
            const float noise = random();

            if (pops > 0 && --untilPop <= 0) {
                engine.afterfire((pops == 2 ? 0.43f : 0.26f) * (1.0f + 0.17f * noise));
                --pops;
                untilPop = 2750 + static_cast<int>(720.0f * noise);
            }
            auto motor = engine.sample(rpm, load, cut, noise);
            if (flatPlane && hasRecordings) {
                const float recording = recordedExhaust();
                recordingBody += (recording - recordingBody) * mix(0.072f, 0.48f, load);
                const float recordingGain = smooth(1680.0f, 3100.0f, rpm) * mix(0.51f, 0.91f, load);
                for (auto& channel : motor) {
                    channel = mix(channel, (recordingBody + (recording - recordingBody) * load * 0.55f) *
                        (0.40f + 1.17f * load) * cut, recordingGain);
                }
            }

            road += (noise - road) * 0.038f;
            roadHigh += (noise - roadHigh) * 0.28f;
            const float air = (road * 0.13f + (roadHigh - road) * 0.035f) * smooth(5.0f, 59.0f, speed);
            float tire = tires.sample(0.83f + 0.26f * smooth(5.0f, 47.0f, speed) + slip * 0.12f);
            if (tires.waveform.empty()) tire = roadHigh - road;
            tire *= slip * (0.23f + 0.43f * slip);
            for (std::size_t side = 0; side < motor.size(); ++side) {
                const float signal = motor[side] * (flatPlane ? 1.11f : 1.26f) +
                    air * (side == 0 ? 1.0f : 0.87f) + tire;
                dc[side] += (signal - dc[side]) * 0.0019f;
                const float centered = (signal - dc[side]) * gain;
                output[frame * 2 + side] = centered / (1.0f + 0.45f * std::abs(centered));
            }
        }
    }
};

GameAudio::GameAudio(const std::filesystem::path& assets) : playback_(std::make_unique<Playback>(assets)) {}
GameAudio::~GameAudio() = default;

void GameAudio::update(CarModel model, const CarState& car, const DriveInput& input, bool driving)
{
    float skid = 0.0f, totalSlip = 0.0f;
    for (const auto& wheel : car.wheels) {
        skid = std::max(skid, wheel.slip);
        totalSlip += wheel.slip;
    }
    const float speed = glm::length(car.velocity);
    const float motion = smooth(1.0f, 6.0f, speed);
    const float load = driving && !input.serviceBrake ? std::abs(input.throttle) : 0.0f;
    playback_->gale.store(model == CarModel::Gale, std::memory_order_relaxed);
    playback_->targetRpm.store(std::max(car.rpm, idleRpm), std::memory_order_relaxed);
    playback_->targetThrottle.store(load, std::memory_order_relaxed);
    playback_->targetSlip.store(driving ? (skid * 0.76f + totalSlip * 0.06f) * motion : 0.0f, std::memory_order_relaxed);
    playback_->targetSpeed.store(driving ? speed : 0.0f, std::memory_order_relaxed);
    playback_->shifting.store(driving && car.shiftTime > 0.0f, std::memory_order_relaxed);
    playback_->targetGain.store(driving ? 0.82f : 0.0f, std::memory_order_relaxed);
}
