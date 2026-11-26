#pragma once

#include "driving.hpp"

#include <filesystem>
#include <memory>

class GameAudio {
public:
    explicit GameAudio(const std::filesystem::path& assets);
    ~GameAudio();
    GameAudio(const GameAudio&) = delete;
    GameAudio& operator=(const GameAudio&) = delete;
    void update(CarModel model, const CarState& car, const DriveInput& input, bool driving);

private:
    struct Playback;
    std::unique_ptr<Playback> playback_;
};
