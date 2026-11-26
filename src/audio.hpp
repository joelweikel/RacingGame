#pragma once

#include "driving.hpp"

#include <memory>

class GameAudio {
public:
    GameAudio();
    ~GameAudio();
    GameAudio(const GameAudio&) = delete;
    GameAudio& operator=(const GameAudio&) = delete;
    void update(CarModel model, const CarState& car, const DriveInput& input, bool driving);

private:
    struct Playback;
    std::unique_ptr<Playback> playback_;
};
