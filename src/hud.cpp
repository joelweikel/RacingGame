#include "hud.hpp"

#include <algorithm>
#include <cmath>
#include <string>

void drawHud(UiDraw& ui, const CarState& car, const CarSpec& spec, std::string_view course, bool controller)
{
    constexpr glm::vec4 ink{0.035f, 0.10f, 0.17f, 0.94f};
    constexpr glm::vec4 cyan{0.34f, 0.97f, 0.94f, 1.0f};
    constexpr glm::vec4 white{0.94f, 0.98f, 1.0f, 1.0f};
    constexpr glm::vec4 muted{0.63f, 0.79f, 0.86f, 1.0f};
    constexpr glm::vec4 flame{1.0f, 0.45f, 0.29f, 1.0f};
    constexpr auto display = UiFont::Display;
    ui.poly({{24, 28}, {235, 28}, {219, 86}, {24, 86}}, ink);
    ui.rect({24, 28, 4, 58}, cyan);
    ui.text(course, {42, 37}, 23, white, display);
    ui.text(spec.name, {43, 62}, 15, muted);
    ui.rounded({1158, 29, 96, 29}, 14, ink);
    ui.text(controller ? "+   Pause" : "Esc   Pause", {1170, 34}, 15, white);

    ui.poly({{945, 538}, {1250, 538}, {1250, 695}, {909, 695}}, {0.01f, 0.04f, 0.075f, 0.32f});
    ui.poly({{942, 532}, {1246, 532}, {1246, 689}, {906, 689}}, ink);
    ui.poly({{942, 532}, {1246, 532}, {1246, 536}, {941, 536}}, cyan);
    ui.poly({{1149, 549}, {1234, 549}, {1234, 637}, {1137, 637}}, {0.12f, 0.27f, 0.38f, 0.83f});
    ui.line({1134, 548}, {1117, 636}, 1.5f, {0.40f, 0.70f, 0.81f, 0.38f});

    const long mph = std::min(999L, std::lround(glm::length(car.velocity) * metersPerSecondToMph));
    const auto speed = std::to_string(mph);
    ui.text(speed, {957, 536}, 88, white, display);
    ui.text("MPH", {964, 611}, 17, muted);
    const float revs = std::clamp(car.rpm / spec.torqueCurve.back().rpm, 0.0f, 1.0f);
    const float shiftAmount = std::clamp(car.shiftTime * 5.5f, 0.0f, 1.0f);
    const glm::vec4 gear = glm::mix(cyan, white, shiftAmount);
    ui.text("GEAR", {1170, 555}, 14, muted);
    ui.text(car.gear < 0 ? "R" : std::to_string(car.gear), {1174, 567 - shiftAmount * 4}, 61, gear, display);
    ui.text(car.automatic ? "AUTO" : "MANUAL", {1171, 619}, 13, muted);

    constexpr int segments = 22;
    for (int i = 0; i < segments; ++i) {
        const float x = 940.0f + i * 13.45f;
        const glm::vec4 on = i >= 18 ? flame : (i >= 14 ? glm::vec4(0.94f, 0.83f, 0.41f, 1.0f) : cyan);
        const float energy = std::clamp(revs * segments - i, 0.0f, 1.0f);
        const auto fill = glm::mix(glm::vec4(0.15f, 0.27f, 0.36f, 1.0f), on, energy);
        ui.poly({{x + 4, 642}, {x + 14, 642}, {x + 10, 655}, {x, 655}}, fill);
    }
    ui.text("RPM", {943, 662}, 14, muted);
    ui.text(std::to_string(static_cast<int>(car.rpm / 50.0f) * 50), {986, 660}, 19, white, display);
    if (revs > 0.88f && car.gear > 0 && car.gear < highestGear) {
        ui.text("SHIFT", {1170, 659}, 20, flame, display);
    }
}
