#pragma once

#include "driving.hpp"
#include "ui_draw.hpp"

#include <string_view>

void drawHud(UiDraw& ui, const CarState& car, const CarSpec& spec, std::string_view course, bool controller);
