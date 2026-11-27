#include "hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

constexpr glm::vec4 ink{0.018f, 0.018f, 0.021f, 1.0f};
constexpr glm::vec4 graphite{0.12f, 0.12f, 0.13f, 1.0f};
constexpr glm::vec4 white{0.98f, 0.99f, 1.0f, 1.0f};
constexpr glm::vec4 muted{0.76f, 0.78f, 0.81f, 1.0f};
constexpr glm::vec4 red{1.0f, 0.18f, 0.24f, 1.0f};
constexpr glm::vec4 dimRed{0.35f, 0.055f, 0.075f, 1.0f};
constexpr glm::vec4 pale{0.88f, 0.94f, 0.97f, 1.0f};
constexpr glm::vec4 navy{0.035f, 0.12f, 0.29f, 1.0f};
constexpr auto display = UiFont::Display;
constexpr float pi = 3.1415926536f;

void frame(UiDraw& ui, UiBox b, float radius, bool light)
{
    ui.rounded({b.x, b.y + 4, b.w, b.h}, radius, {0.014f, 0.017f, 0.023f, 0.24f});
    ui.beveled(b, radius, light ? white : graphite, light ? glm::vec4(0.93f, 0.96f, 0.98f, 1) : ink,
               light ? glm::vec4(1) : muted * glm::vec4(0.52f, 0.52f, 0.52f, 1),
               light ? glm::vec4(0.59f, 0.68f, 0.74f, 1) : ink);
}

void indicators(UiDraw& ui, UiBox view, std::string_view course, std::string_view car,
                std::string_view pauseKey, bool active, bool gale)
{
    const float x = view.x + 24, y = view.y + 26;
    const auto label = gale ? ink : white;
    frame(ui, {x, y, 189, 59}, 12, gale);
    ui.text(course, {x + 15, y + 5}, 26, label, display);
    ui.text(car, {x + 16, y + 34}, 16, gale ? ink : muted);
    if (active) {
        const float keyWidth = ui.measure(pauseKey, 16);
        const float w = keyWidth + ui.measure("Pause", 16) + 34;
        const float left = view.x + view.w - 24 - w;
        frame(ui, {left, y, w, 33}, 9, gale);
        ui.text(pauseKey, {left + 12, y + 7}, 16, gale ? ink : uiAccentBlue);
        ui.text("Pause", {left + 22 + keyWidth, y + 7}, 16, label);
    }
}

void galeHud(UiDraw& ui, glm::vec2 corner, const CarState& car, const CarSpec& spec, std::string_view speed)
{
    constexpr glm::vec2 size{338, 145};
    const auto p = corner - size;
    frame(ui, {p.x, p.y, size.x, size.y}, 16, true);
    constexpr auto speedFont = UiFont::SpectralSpeed;
    constexpr float speedSize = 89.0f;
    ui.text(speed, p + glm::vec2(18, -2), speedSize, navy, speedFont);
    ui.text("MPH", p + glm::vec2(24 + ui.measure(speed, speedSize, speedFont), 54), 17, ink);
    ui.line(p + glm::vec2(189, 16), p + glm::vec2(189, 79), 1, {0.57f, 0.62f, 0.67f, 0.55f});
    ui.text("GEAR", p + glm::vec2(208, 20), 16, ink);
    ui.text(car.gear < 0 ? "R" : std::to_string(car.gear), p + glm::vec2(278, 13), 54, ink, display);
    ui.text(car.automatic ? "AUTO" : "MANUAL", p + glm::vec2(208, 45), 16, ink);
    if (car.brakeLights) {
        ui.circle(p + glm::vec2(212, 75), 3, red);
        ui.text("BRAKE", p + glm::vec2(221, 66), 14, ink);
    }

    constexpr int segments = 23;
    const float revs = std::clamp(car.rpm / spec.torqueCurve.back().rpm, 0.0f, 1.0f);
    for (int i = 0; i < segments; ++i) {
        const auto fill = glm::mix(glm::vec4(0.76f, 0.81f, 0.85f, 1), uiAccentBlue,
                                   std::clamp(revs * segments - i, 0.0f, 1.0f));
        ui.rounded({p.x + 18 + i * 13.25f, p.y + 94, 10.5f, 7}, 2, fill);
    }
    const int rpm = static_cast<int>(car.rpm / 50.0f) * 50;
    ui.text(std::to_string(rpm) + " RPM", p + glm::vec2(18, 113), 18, ink);
    if (car.rpm >= spec.upshiftRpm && car.gear > 0 && car.gear < highestGear) {
        ui.text("SHIFT", p + glm::vec2(276, 113), 17, ink);
    }
}

glm::vec2 dialPoint(float angle)
{
    return {std::cos(angle), std::sin(angle)};
}

void tachometer(UiDraw& ui, glm::vec2 center, const CarState& car, const CarSpec& spec, bool active)
{
    constexpr float start = pi * 0.75f, sweep = pi * 1.5f;
    const int thousands = static_cast<int>(std::ceil(spec.torqueCurve.back().rpm / 1000.0f));
    const float scale = thousands * 1000.0f;
    const float redline = start + sweep * (spec.upshiftRpm / scale);
    const float angle = start + sweep * std::clamp(car.rpm / scale, 0.0f, 1.0f);

    ui.circle(center + glm::vec2(0, 4), 95, {0.015f, 0.015f, 0.02f, 0.32f});
    ui.circle(center, 95, dimRed);
    ui.circle(center, 92, graphite);
    ui.circle(center, 87, ink);
    ui.arc(center, {81, 81}, start, start + sweep, 4, dimRed);
    ui.arc(center, {81, 81}, redline, start + sweep, 4, red);
    ui.arc(center, {81, 81}, start, angle, 4, red);

    constexpr int ticksPerMark = 4;
    const int tickCount = thousands * ticksPerMark;
    for (int i = 0; i <= tickCount; ++i) {
        const float tickAngle = start + sweep * (static_cast<float>(i) / tickCount);
        const auto direction = dialPoint(tickAngle);
        const bool major = i % ticksPerMark == 0;
        const auto color = tickAngle >= redline ? red : muted;
        ui.line(center + direction * (major ? 66.0f : 70.0f), center + direction * 76.0f, major ? 2.0f : 1.0f, color);
        if (major) {
            const auto digit = std::to_string(i / ticksPerMark);
            const auto label = center + direction * 54.0f;
            ui.text(digit, label - glm::vec2(ui.measure(digit, 21, display) * 0.5f, 11), 21, white, display);
        }
    }
    const auto direction = dialPoint(angle);
    const glm::vec2 normal{-direction.y, direction.x};
    ui.poly({center - direction * 10.0f + normal * 3.2f, center + direction * 73.0f,
             center - direction * 10.0f - normal * 3.2f}, red);
    ui.circle(center, 8, red);
    ui.circle(center, 3.5f, ink);
    const std::string gear = car.gear < 0 ? "R" : std::to_string(car.gear);
    ui.text(gear, center - glm::vec2(6, -16), 25, white, display);
    ui.text("RPM x1000", center - glm::vec2(27, -60), 14, muted);
    if (active) ui.bloom({center.x - 4, center.y - 4, 8, 8}, {red.r, red.g, red.b, 0.24f});
}

void digitalSpeed(UiDraw& ui, glm::vec2 p, std::string_view speed)
{
    constexpr std::array<unsigned, 10> digits{0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    constexpr std::array<UiBox, 7> segments{{
        {4, 0, 21, 4}, {25, 4, 4, 17}, {25, 25, 4, 17}, {4, 42, 21, 4},
        {0, 25, 4, 17}, {0, 4, 4, 17}, {4, 21, 21, 4}
    }};
    constexpr int places = 3;
    for (int i = 0; i < places; ++i) {
        const int digit = i - (places - static_cast<int>(speed.size()));
        const unsigned lit = digit < 0 ? 0 : digits[speed[digit] - '0'];
        for (std::size_t j = 0; j < segments.size(); ++j) {
            auto segment = segments[j];
            segment.x += p.x + i * 36.0f;
            segment.y += p.y;
            ui.rounded(segment, 1.4f, (lit & (1u << j)) ? red : glm::vec4(0.17f, 0.029f, 0.041f, 1));
        }
    }
}

void slimHud(UiDraw& ui, glm::vec2 corner, const CarState& car, const CarSpec& spec, std::string_view speed, bool active)
{
    const auto p = corner - glm::vec2(170, 148);
    frame(ui, {p.x, p.y, 170, 120}, 15, false);
    ui.rounded({p.x + 17, p.y + 15, 137, 60}, 8, {0.055f, 0.018f, 0.024f, 1});
    digitalSpeed(ui, p + glm::vec2(35, 22), speed);
    ui.text("MPH", p + glm::vec2(112, 78), 19, white, display);
    ui.text(car.automatic ? "AUTO" : "MANUAL", p + glm::vec2(20, 94), 14, muted);
    if (car.brakeLights) {
        ui.text("BRAKE", p + glm::vec2(109, 96), 13, red);
    } else if (car.rpm >= spec.upshiftRpm && car.gear > 0 && car.gear < highestGear) {
        ui.text("SHIFT", p + glm::vec2(111, 96), 13, red);
    }
    tachometer(ui, corner - glm::vec2(244, 96), car, spec, active);
}

} // namespace

void drawHud(UiDraw& ui, const CarState& car, CarModel model, std::string_view course,
             std::string_view pauseKey, bool active)
{
    const auto& spec = carSpec(model);
    const auto view = ui.viewportBounds();
    const glm::vec2 corner{view.x + view.w - 25, view.y + view.h - 25};
    const auto mph = std::min(999L, std::lround(glm::length(car.velocity) * metersPerSecondToMph));
    const auto speed = std::to_string(mph);
    const bool gale = model == CarModel::Gale;
    indicators(ui, view, course, spec.name, pauseKey, active, gale);
    if (gale) galeHud(ui, corner, car, spec, speed);
    else slimHud(ui, corner, car, spec, speed, active);
}
