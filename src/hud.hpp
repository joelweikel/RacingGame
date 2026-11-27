#pragma once

#include "driving.hpp"
#include "ui_draw.hpp"

#include <string_view>

void drawHud(UiDraw& ui, const CarState& car, CarModel model, std::string_view course,
             std::string_view pauseKey, bool active);
