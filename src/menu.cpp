#include "menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

EM_JS(void, readGaragePreferences, (char* out, int capacity), {
    try { stringToUTF8(localStorage.getItem("racinggame.garage.v1") || "", out, capacity); } catch (_) {}
});
EM_JS(void, writeGaragePreferences, (const char* data), {
    try { localStorage.setItem("racinggame.garage.v1", UTF8ToString(data)); } catch (_) {}
});
EM_JS(void, readGraphicsPreferences, (char* out, int capacity), {
    try { stringToUTF8(localStorage.getItem("racinggame.graphics.v1") || "", out, capacity); } catch (_) {}
});
EM_JS(void, writeGraphicsPreferences, (const char* data), {
    try { localStorage.setItem("racinggame.graphics.v1", UTF8ToString(data)); } catch (_) {}
});
#endif

namespace {

constexpr glm::vec4 ink{0.018f, 0.018f, 0.022f, 1.0f};
constexpr glm::vec4 dark{0.11f, 0.11f, 0.12f, 1.0f};
constexpr glm::vec4 muted{0.76f, 0.79f, 0.82f, 1.0f};
constexpr glm::vec4 white{0.97f, 0.995f, 1.0f, 1.0f};
constexpr glm::vec4 pale{0.79f, 0.89f, 0.96f, 1.0f};
constexpr glm::vec4 buttonBlue{0.07f, 0.31f, 0.72f, 1.0f};
constexpr glm::vec4 deepBlue{0.045f, 0.18f, 0.49f, 1.0f};
constexpr glm::vec4 rail{0.72f, 0.78f, 0.83f, 0.36f};
constexpr glm::vec4 rim{0.39f, 0.41f, 0.43f, 1.0f};
constexpr glm::vec4 shadow{0.018f, 0.025f, 0.034f, 0.19f};
constexpr float pi = 3.141592654f;
constexpr auto italic = UiFont::Display;
constexpr UiBox backButton{1088, 39, 112, 38};
constexpr UiBox padTab{96, 232, 138, 36};
constexpr UiBox keysTab{242, 232, 138, 36};
constexpr UiBox defaultsButton{96, 624, 164, 35};
constexpr UiBox settingsPanel{72, 211, 1128, 465};
constexpr std::array settingsTabs{"Controls", "Graphics", "Credits"};
constexpr std::array qualityLabels{"Automatic", "Low", "Medium", "High"};
constexpr std::array scaleLabels{"67%", "85%", "100%"};
constexpr int graphicsRows = 5;
constexpr int bindingRows = actionCount / 2;

UiBox settingsTabBox(int i) { return {72.0f + i * 166.0f, 152, 156, 41}; }
UiBox bindingBox(int i) { return {96.0f + (i / bindingRows) * 552.0f, 285.0f + (i % bindingRows) * 41.0f, 528, 36}; }
UiBox graphicsBox(int i) { return {96, 231.0f + i * 76.0f, 1080, 66}; }
UiBox creditBox(int i) { return {96, 240.0f + i * 55.0f, 324, 44}; }
constexpr UiBox goButton{842, 560, 358, 70};
constexpr std::array courses{Course::TestGrid, Course::DustlineOval, Course::DerelictCamp};
constexpr UiBox startButton{72, 488, 340, 72};
constexpr UiBox garagePreview{550, 145, 665, 400};
constexpr int garageContinue = 7;
constexpr std::array garageLabels{"Red", "Green", "Blue", "Hood intake", "Splitter", "Rear wing", "Engine"};
constexpr std::array garageParts{CarPart::HoodIntake, CarPart::Splitter, CarPart::Wing, CarPart::Diesel};
constexpr std::array garageToggles{&CarSetup::hoodIntake, &CarSetup::splitter, &CarSetup::wing, &CarSetup::diesel};

UiBox paintSlider(UiBox row) { return {row.x + 126, row.y + 12, 234, 17}; }

enum class Icon { Play, Players, Controls, Car, Map, Oval, Restart, Exit };

std::string_view manufacturer(CarModel model)
{
    return model == CarModel::Gale ? "Joeltech" : "Slim Motorsports";
}

void centered(UiDraw& ui, std::string_view name, glm::vec2 center, float size, glm::vec4 color, UiFont font = UiFont::Label)
{
    ui.text(name, {center.x - ui.measure(name, size, font) * 0.5f, center.y - size * 0.49f}, size, color, font);
}

glm::vec4 opacity(glm::vec4 color, float alpha)
{
    color.a = alpha;
    return color;
}

float colorReveal(float time)
{
    return glm::smoothstep(0.45f, 2.55f, time);
}

void panel(UiDraw& ui, UiBox b, float radius = 15.0f)
{
    ui.rounded({b.x, b.y + 4, b.w, b.h}, radius, shadow);
    ui.beveled(b, radius, dark, ink, rim, ink);
}

void stamp(UiDraw& ui, std::string_view label, UiBox b, bool selected)
{
    ui.beveled(b, 9, selected ? buttonBlue : dark,
               selected ? deepBlue : ink, selected ? uiAccentBlue : rim, selected ? deepBlue : ink);
    centered(ui, label, b.center(), 17, white);
}

void icon(UiDraw& ui, Icon kind, glm::vec2 p, glm::vec4 color)
{
    const auto line = [&](glm::vec2 a, glm::vec2 b) { ui.line(p + a, p + b, 1.6f, color); };
    switch (kind) {
    case Icon::Play:
        ui.poly({p + glm::vec2(-5, -9), p + glm::vec2(9, 0), p + glm::vec2(-5, 9)}, color);
        break;
    case Icon::Players:
        ui.circle(p + glm::vec2(-4, -4), 3.5f, color);
        ui.circle(p + glm::vec2(5, -2), 2.6f, color);
        ui.arc(p + glm::vec2(-4, 7), {6, 6}, pi, 2 * pi, 1.6f, color);
        ui.arc(p + glm::vec2(5, 7), {4, 4}, 1.35f * pi, 2 * pi, 1.6f, color);
        break;
    case Icon::Controls:
        for (int i = -1; i <= 1; ++i) {
            const float y = i * 5.5f;
            line({-9, y}, {9, y});
            ui.circle(p + glm::vec2(i * 4, y), 2.6f, color);
        }
        break;
    case Icon::Car:
        line({-10, 4}, {-10, 0});
        line({-10, 0}, {-5, -5});
        line({-5, -5}, {4, -5});
        line({4, -5}, {10, 0});
        line({-10, 0}, {10, 0});
        line({10, 0}, {10, 4});
        line({-10, 4}, {10, 4});
        ui.circle(p + glm::vec2(-6, 5), 2.5f, color);
        ui.circle(p + glm::vec2(6, 5), 2.5f, color);
        break;
    case Icon::Map:
        ui.arc(p, {7, 7}, 0, 2 * pi, 1.6f, color);
        line({-11, 0}, {-4, 0});
        line({4, 0}, {11, 0});
        line({0, -11}, {0, -4});
        line({0, 4}, {0, 11});
        break;
    case Icon::Oval:
        ui.arc(p, {10, 6}, 0, 2 * pi, 2, color);
        line({3, 3}, {3, 9});
        break;
    case Icon::Restart:
        ui.arc(p, {8, 8}, -1.2f, 3.7f, 1.7f, color);
        line({-7, -4}, {-7, -10});
        line({-7, -4}, {-1, -4});
        break;
    case Icon::Exit:
        line({-9, -8}, {-9, 8});
        line({-9, -8}, {-2, -8});
        line({-9, 8}, {-2, 8});
        line({-4, 0}, {9, 0});
        line({9, 0}, {5, -4});
        line({9, 0}, {5, 4});
        break;
    }
}

void button(UiDraw& ui, UiBox box, std::string_view text, bool focused, float time,
            Icon glyph, std::string_view description = {}, float press = 0.0f, bool primary = false)
{
    box.x += press * 5.0f;
    box.y += press * 3.0f;
    box.w -= press * 10.0f;
    box.h -= press * 6.0f;
    const float right = box.x + box.w, foot = box.y + box.h;
    const auto accent = primary ? white : (focused ? uiAccentBlue : muted);
    const auto top = primary ? buttonBlue :
        (focused ? glm::vec4(0.075f, 0.13f, 0.23f, 1.0f) : dark);
    ui.rounded({box.x, box.y + 4, box.w, box.h}, 14, shadow);
    ui.beveled(box, 14, top, primary ? deepBlue : ink, focused ? uiAccentBlue : rim, focused ? deepBlue : ink);
    icon(ui, glyph, {box.x + 28, box.center().y}, accent);
    const float nameY = description.empty() ? box.center().y - 17 : box.y + 7;
    ui.text(text, {box.x + 59, nameY}, 35, white, italic);
    if (!description.empty()) {
        ui.text(description, {box.x + 61, box.y + 42}, 17, primary ? white : muted);
    }
    const glm::vec2 edge{right - 25 + (focused ? 2.0f * std::sin(time * 5) : 0.0f), box.center().y};
    ui.line(edge - glm::vec2(6, 6), edge, 1.8f, accent);
    ui.line(edge, edge - glm::vec2(6, -6), 1.8f, accent);
    if (focused) {
        const float pulse = 0.29f + 0.08f * std::sin(time * 4.0f);
        ui.bloom({box.x + 16, box.y, box.w - 32, 5}, opacity(uiAccentBlue, pulse));
        const float gleam = box.x + 65 + (box.w - 127) * std::fmod(time * 0.46f, 1.0f);
        ui.rounded({gleam, foot - 3, 22, 1.5f}, 0.75f, primary ? pale : uiAccentBlue);
        ui.bloom({gleam, foot - 5, 24, 5}, opacity(uiAccentBlue, 0.43f));
    }
}

float noise(std::uint32_t seed)
{
    seed = (seed ^ (seed >> 16)) * 0x45d9f3bu;
    seed = (seed ^ (seed >> 16)) * 0x45d9f3bu;
    return static_cast<float>((seed ^ (seed >> 16)) & 0xffffu) / 65535.0f;
}

void spark(UiDraw& ui, glm::vec2 p, float size, glm::vec4 color, bool bright)
{
    ui.poly({p + glm::vec2(0, -size), p + glm::vec2(size, 0),
             p + glm::vec2(0, size), p + glm::vec2(-size, 0)}, color);
    if (bright) {
        ui.line(p - glm::vec2(size * 2.5f, 0), p + glm::vec2(size * 2.5f, 0), 1, color);
        ui.bloom({p.x - 4, p.y - 4, 8, 8}, opacity(color, color.a * 0.64f));
    }
}

void ambientParticles(UiDraw& ui, float time, float strength)
{
    for (std::uint32_t i = 1; i <= 92; ++i) {
        const float depth = noise(5 * i);
        const float travel = std::fmod(noise(5 * i + 1) + time * (0.012f + depth * 0.031f), 1.0f);
        const float y = 20 + noise(5 * i + 2) * 645 + std::sin(time * 0.55f + i) * (3 + 9 * depth);
        const glm::vec2 p{18 + travel * 1244, y};
        const float twinkle = 0.5f + 0.5f * std::sin(time * (0.7f + depth) + i);
        const float pulse = 0.42f + 0.58f * twinkle * twinkle;
        const float edgeFade = std::min(1.0f, std::min(travel, 1 - travel) * 16.0f);
        const bool bright = i % 11 == 0;
        const auto color = opacity(i % 7 == 0 ? uiAccentBlue : white, (0.24f + depth * 0.46f) * pulse * edgeFade * strength);
        spark(ui, p, 0.8f + depth * 1.5f, color, bright);
    }
}

void transitionParticles(UiDraw& ui, glm::vec2 origin, float age)
{
    const float fade = std::clamp(1.0f - age / 0.55f, 0.0f, 1.0f);
    for (std::uint32_t i = 1; i <= 26; ++i) {
        const float angle = noise(i * 3) * 2 * pi;
        const glm::vec2 direction{std::cos(angle), std::sin(angle)};
        const glm::vec2 p = origin + direction * (18.0f + age * (130 + noise(i * 3 + 1) * 530));
        const auto color = opacity(i % 3 == 0 ? pale : uiAccentBlue, fade * (0.45f + noise(i * 3 + 2) * 0.55f));
        ui.line(p - direction * (8.0f + 18.0f * fade), p, 1.4f, opacity(color, color.a * 0.5f));
        spark(ui, p, 1 + 1.5f * fade, color, i % 4 == 0);
    }
}

void keyHint(UiDraw& ui, glm::vec2 at, std::string_view key, std::string_view action)
{
    const float width = ui.measure(key, 15) + 19;
    ui.roundedGradient({at.x, at.y, width, 22}, 6, dark, ink);
    ui.text(key, at + glm::vec2(9, 2), 15, white);
    ui.text(action, at + glm::vec2(width + 9, 1), 17, ink);
}

void logo(UiDraw& ui, float y)
{
    ui.text("MMORP", {72, y + 3}, 157, opacity(dark, 0.14f), italic);
    ui.text("MMORP", {67, y}, 157, ink, italic);
    ui.text("RACER", {73, y + 112}, 112, buttonBlue, italic);
}

void frameCorners(UiDraw& ui, UiBox b, glm::vec4 color)
{
    for (const float x : {-1.0f, 1.0f}) {
        for (const float y : {-1.0f, 1.0f}) {
            const auto p = b.center() + glm::vec2(x * b.w, y * b.h) * 0.5f;
            ui.line(p, p - glm::vec2(x * 23, 0), 2, color);
            ui.line(p, p - glm::vec2(0, y * 15), 2, color);
        }
    }
}

int horsepower(const CarSpec& spec)
{
    float power = 0;
    for (std::size_t i = 1; i < spec.torqueCurve.size(); ++i) {
        const auto a = spec.torqueCurve[i - 1], b = spec.torqueCurve[i];
        const float slope = (b.newtonMeters - a.newtonMeters) / (b.rpm - a.rpm);
        const float intercept = a.newtonMeters - a.rpm * slope;
        const float peak = slope < 0 ? std::clamp(-0.5f * intercept / slope, a.rpm, b.rpm) : b.rpm;
        power = std::max(power, peak * (intercept + slope * peak));
    }
    return static_cast<int>(std::lround(power * (2.0f * pi / (60.0f * 745.7f))));
}

void driveSplit(UiDraw& ui, const CarSpec& spec)
{
    constexpr UiBox b{72, 446, 456, 139};
    panel(ui, b);
    ui.text("DRIVE DISTRIBUTION", {90, 458}, 17, white);
    const int rear = static_cast<int>(std::lround(spec.rearDriveFraction * 100.0f));
    const auto frontLabel = "Front  " + std::to_string(100 - rear) + "%";
    const auto rearLabel = "Rear  " + std::to_string(rear) + "%";
    ui.text(frontLabel, {90, 541}, 20, white);
    ui.text(rearLabel, {510 - ui.measure(rearLabel, 20), 541}, 20, white);
    constexpr UiBox bar{90, 501, 420, 24};
    ui.roundedGradient(bar, 6, dark, ink);
    for (int i = 0; i < 20; ++i) {
        const float x = bar.x + 5 + i * 20.6f;
        const auto fill = i < (100 - rear) / 5 ? pale : uiAccentBlue;
        ui.roundedGradient({x, bar.y + 4, 17, 16}, 3, fill, opacity(fill, 0.58f));
    }
}

void drawCourse(UiDraw& ui, Course course, float time)
{
    const bool oval = course == Course::DustlineOval;
    constexpr UiBox bounds{596, 158, 604, 382};
    constexpr UiBox map{611, 173, 574, 352};
    const auto line = opacity(pale, 0.14f);
    panel(ui, bounds);
    ui.roundedGradient(map, 10, dark, ink);
    for (int x = 630; x < 1182; x += 24) ui.line({x, 189}, {x, 509}, 1, line);
    for (int y = 190; y < 522; y += 24) ui.line({625, y}, {1171, y}, 1, line);
    frameCorners(ui, {623, 189, 550, 322}, opacity(pale, 0.4f));
    if (oval) {
        const auto route = [&](float width, glm::vec4 fill) {
            ui.line({790, 295}, {1006, 295}, width, fill);
            ui.line({790, 435}, {1006, 435}, width, fill);
            ui.arc({790, 365}, {70, 70}, pi * 0.5f, pi * 1.5f, width, fill);
            ui.arc({1006, 365}, {70, 70}, -pi * 0.5f, pi * 0.5f, width, fill);
        };
        route(33, opacity(uiAccentBlue, 0.30f));
        route(26, uiAccentBlue);
        route(21, ink);
        route(2, opacity(pale, 0.48f));
        for (int i = 0; i < 8; ++i) {
            ui.rect({922.0f + 5.0f * (i % 2), 425.0f + 5.0f * (i / 2), 5, 5},
                    (i + i / 2) % 2 == 0 ? white : ink);
        }
        constexpr float straight = 216, turn = pi * 70;
        const float lap = std::fmod(time * 97.0f, 2 * (straight + turn));
        const float direction = lap < straight + turn ? 1.0f : -1.0f;
        const float s = std::fmod(lap, straight + turn);
        glm::vec2 marker;
        if (s < straight) {
            marker = glm::vec2(898, 365) + direction * glm::vec2(s - straight * 0.5f, -70);
        } else {
            const float angle = (s - straight) / 70 - pi * 0.5f;
            marker = glm::vec2(898, 365) +
                     direction * (glm::vec2(straight * 0.5f, 0) + 70.0f * glm::vec2(std::cos(angle), std::sin(angle)));
        }
        spark(ui, marker, 5, white, true);
    } else if (course == Course::DerelictCamp) {
        constexpr std::array<glm::vec2, 8> route{{{696, 493}, {766, 416}, {889, 430}, {991, 375},
                                                {952, 313}, {819, 327}, {771, 280}, {871, 218}}};
        for (std::size_t i = 1; i < route.size(); ++i) {
            ui.line(route[i - 1], route[i], 13, opacity(uiAccentBlue, 0.3f));
            ui.line(route[i - 1], route[i], 3, uiAccentBlue);
        }
        ui.line({958, 393}, {1069, 447}, 2, opacity(pale, 0.7f));
        ui.rect({1066, 443, 12, 9}, pale);
        ui.text("Camp", {1087, 436}, 19, pale);
        const float path = std::fmod(time * 1.8f, static_cast<float>(route.size() - 1));
        const auto i = static_cast<std::size_t>(path);
        spark(ui, glm::mix(route[i], route[i + 1], path - static_cast<float>(i)), 5, white, true);
    } else {
        constexpr glm::vec2 center{898, 365};
        ui.circle(center, 40, opacity(uiAccentBlue, 0.11f));
        for (float radius : {40.0f, 79.0f, 118.0f}) {
            ui.arc(center, {radius, radius}, 0, pi * 2, 1.5f, opacity(pale, 0.28f));
        }
        ui.line({898, 235}, {898, 495}, 1, opacity(pale, 0.52f));
        ui.line({711, 365}, {1085, 365}, 1, opacity(pale, 0.52f));
        const float heading = time * 0.85f;
        ui.arc(center, {118, 118}, heading - 0.36f, heading, 2.5f, uiAccentBlue);
        ui.line(center, center + 118.0f * glm::vec2(std::cos(heading), std::sin(heading)), 1.5f, opacity(uiAccentBlue, 0.72f));
        spark(ui, center, 6.0f + std::sin(time * 4.0f) * 1.2f, uiAccentBlue, true);
    }
}

} // namespace

std::string_view courseName(Course course)
{
    switch (course) {
    case Course::DerelictCamp: return "Derelict Camp";
    case Course::DustlineOval: return "Dustline Oval";
    default: return "Test Grid";
    }
}

Menu::Menu(CarModel initialModel, bool hasGale, const std::filesystem::path& assets)
    : selectedCar_(initialModel), hasGale_(hasGale)
{
    std::ifstream creditFile(assets / "audio-credits.txt");
    std::string line;
    std::getline(creditFile, line); // File title.
    bool newSection = true;
    while (std::getline(creditFile, line)) {
        const auto first = line.find_first_not_of(" \r\t");
        if (first == std::string::npos) {
            newSection = true;
            continue;
        }
        if (newSection) credits_.emplace_back();
        newSection = false;
        credits_.back().push_back(line.substr(first, line.find_last_not_of(" \r\t") - first + 1));
    }
#ifdef __EMSCRIPTEN__
    char graphicsData[64]{};
    readGraphicsPreferences(graphicsData, sizeof(graphicsData));
    std::istringstream graphicsFile(graphicsData);
    char data[128]{};
    readGaragePreferences(data, sizeof(data));
    std::istringstream file(data);
#else
    std::ifstream graphicsFile(preferencesPath("graphics.cfg", "racing-graphics.cfg"));
    std::ifstream file(preferencesPath("garage.cfg", "racing-garage.cfg"));
#endif
    int preset = 0, lod = 0, scale = 0;
    bool bloom = true;
    if (graphicsFile >> preset >> lod >> scale >> bloom &&
        preset >= 0 && preset < static_cast<int>(qualityLabels.size()) &&
        lod >= 0 && lod < static_cast<int>(qualityLabels.size()) &&
        scale >= 0 && scale < static_cast<int>(scaleLabels.size())) {
        graphics_ = {static_cast<QualityLevel>(preset), static_cast<QualityLevel>(lod), scale, bloom};
        bool reflections = true;
        if (graphicsFile >> reflections) graphics_.reflections = reflections;
    }
    for (auto& config : setups_) {
        CarSetup loaded;
        if (!(file >> loaded.paint.r >> loaded.paint.g >> loaded.paint.b >>
              loaded.hoodIntake >> loaded.wing >> loaded.splitter >> loaded.diesel)) return;
        if (loaded.paint == glm::ivec3(-1) ||
            (glm::all(glm::greaterThanEqual(loaded.paint, glm::ivec3(0))) &&
             glm::all(glm::lessThanEqual(loaded.paint, glm::ivec3(255))))) config = loaded;
    }
}

void Menu::saveGraphics() const
{
    std::ostringstream values;
    values << static_cast<int>(graphics_.preset) << ' ' << static_cast<int>(graphics_.lod) << ' '
           << graphics_.renderScale << ' ' << graphics_.bloom << ' ' << graphics_.reflections;
#ifdef __EMSCRIPTEN__
    writeGraphicsPreferences(values.str().c_str());
#else
    const auto path = preferencesPath("graphics.cfg", "racing-graphics.cfg");
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    file << values.str();
#endif
}

void Menu::saveGarage() const
{
    std::ostringstream values;
    for (const auto& config : setups_) {
        values << config.paint.r << ' ' << config.paint.g << ' ' << config.paint.b << ' ' <<
                  config.hoodIntake << ' ' << config.wing << ' ' << config.splitter << ' ' << config.diesel << '\n';
    }
#ifdef __EMSCRIPTEN__
    writeGaragePreferences(values.str().c_str());
#else
    const auto path = preferencesPath("garage.cfg", "racing-garage.cfg");
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    file << values.str();
#endif
}

int Menu::count() const
{
    switch (screen_) {
    case Screen::Main: return 3;
    case Screen::Cars: return hasGale_ ? 2 : 1;
    case Screen::Garage: return garageContinue + 1;
    case Screen::Maps: return static_cast<int>(courses.size());
    case Screen::Pause: return 4;
    default: return 0;
    }
}

UiBox Menu::option(int index) const
{
    switch (screen_) {
    case Screen::Main: return {72, 352.0f + index * 89.0f, 456, 73};
    case Screen::Cars: return {72, 223.0f + index * 97.0f, 456, 73};
    case Screen::Garage:
        return index == garageContinue ? UiBox{72, 568, 456, 63} : UiBox{82, 210.0f + index * 47.0f, 436, 41};
    case Screen::Maps: return {72, 201.0f + index * 82.0f, 456, 73};
    case Screen::Pause: return {424, 232.0f + index * 74.0f, 432, 60};
    default: return {};
    }
}

void Menu::open(Screen next)
{
    if (screen_ == Screen::Garage && next != Screen::Garage) saveGarage();
    screen_ = pending_ = next;
    entered_ = time_;
    capture_ = -1;
    focus_ = next == Screen::Cars && hasGale_ ? (selectedCar_ == CarModel::Gale ? 0 : 1) :
             next == Screen::Maps ? static_cast<int>(std::find(courses.begin(), courses.end(), course_) - courses.begin()) :
             next == Screen::Garage ? garageContinue : next == Screen::Settings ? -1 : 0;
    dragPaint_ = -1;
    orbitDragging_ = false;
    paintRepeat_ = 0.0f;
}

void Menu::transition(Screen next, glm::vec2 origin)
{
    pending_ = next;
    rippleAt_ = time_;
    rippleOrigin_ = origin;
}

bool Menu::takeRaceStart()
{
    const bool started = startingRace_;
    startingRace_ = false;
    return started;
}

void Menu::update(Controls& controls, glm::vec2 cursor, float dt)
{
    time_ += dt;
    const MenuInput& input = controls.menu();
    if (pending_ != screen_) {
        if (time_ - rippleAt_ >= 0.37f) {
            startingRace_ = pending_ == Screen::Drive;
            open(pending_);
        }
        return;
    }
    if (capture_ >= 0) {
        if (controls.gamepadBackHeld() && controllerBindings_) {
            cancelHeld_ += dt;
            if (cancelHeld_ > 0.65f) capture_ = -1;
            return;
        }
        if ((input.click && backButton.contains(cursor)) || (input.back && !controllerBindings_ && controls.usingGamepad())) {
            capture_ = -1;
        }
        else if (controls.capture(capture_, controllerBindings_)) capture_ = -1;
        else if (input.back && !controls.usingGamepad()) capture_ = -1;
        return;
    }
    if (screen_ == Screen::Drive) {
        if (controls.justPressed(Action::Pause)) open(Screen::Pause);
        return;
    }
    if (screen_ == Screen::Splash) {
        if (input.confirm || input.click || controls.justPressed(Action::Pause)) transition(Screen::Main, startButton.center());
        return;
    }
    if (input.back || (input.click && screen_ != Screen::Main && backButton.contains(cursor))) {
        switch (screen_) {
        case Screen::Settings: open(settingsReturn_); break;
        case Screen::Cars: open(Screen::Main); break;
        case Screen::Garage: open(Screen::Cars); break;
        case Screen::Maps: open(Screen::Garage); break;
        case Screen::Pause: open(Screen::Drive); break;
        case Screen::Main: open(Screen::Splash); break;
        default: break;
        }
        return;
    }

    if (screen_ == Screen::Settings) {
        updateSettings(controls, cursor);
        return;
    }
    if (screen_ == Screen::Garage) {
        garageCamera_.orbit(controls.look() * (320.0f * dt));
        orbitDragging_ = input.mouseHeld && (orbitDragging_ || (input.click && garagePreview.contains(cursor)));
        if (orbitDragging_) {
            if (!input.click) garageCamera_.orbit(cursor - orbitCursor_);
            orbitCursor_ = cursor;
            return;
        }
    }
    const int size = count();
    const bool sidewaysNavigation = screen_ != Screen::Garage;
    const int move = static_cast<int>(input.down || (sidewaysNavigation && input.right)) -
                     static_cast<int>(input.up || (sidewaysNavigation && input.left));
    if (move != 0 && size > 0) focus_ = (focus_ + move + size) % size;

    bool activated = input.confirm;
    if (input.click || input.mouseMoved) {
        for (int i = 0; i < size; ++i) {
            if (option(i).contains(cursor)) {
                focus_ = i;
                activated |= input.click && screen_ != Screen::Maps;
                break;
            }
        }
    }
    if (screen_ == Screen::Cars) selectedCar_ = hasGale_ && focus_ == 0 ? CarModel::Gale : CarModel::SlimRider;
    if (screen_ == Screen::Maps) {
        course_ = courses[focus_];
        if (input.click && goButton.contains(cursor)) activated = true;
    }
    if (screen_ == Screen::Garage && updateGarage(input, cursor, dt, activated)) return;
    if (!activated) return;

    switch (screen_) {
    case Screen::Main:
        if (focus_ == 0) transition(Screen::Cars, option(0).center());
        if (focus_ == 2) { settingsReturn_ = Screen::Main; open(Screen::Settings); }
        break;
    case Screen::Cars: open(Screen::Garage); break;
    case Screen::Garage: open(Screen::Maps); break;
    case Screen::Maps: transition(Screen::Drive, goButton.center()); break;
    case Screen::Pause:
        if (focus_ == 0) open(Screen::Drive);
        if (focus_ == 1) { startingRace_ = true; open(Screen::Drive); }
        if (focus_ == 2) { settingsReturn_ = Screen::Pause; open(Screen::Settings); }
        if (focus_ == 3) open(Screen::Main);
        break;
    default: break;
    }
}

void Menu::updateSettings(Controls& controls, glm::vec2 cursor)
{
    const auto& input = controls.menu();
    const int horizontal = static_cast<int>(input.right) - static_cast<int>(input.left);
    const int vertical = static_cast<int>(input.down) - static_cast<int>(input.up);
    const auto cycle = [](int value, int step, int size) { return (value + step + size) % size; };
    if (input.click) {
        for (int i = 0; i < static_cast<int>(settingsTabs.size()); ++i) {
            if (settingsTabBox(i).contains(cursor)) {
                settingsTab_ = static_cast<SettingsTab>(i);
                focus_ = -1;
                return;
            }
        }
    }
    if (focus_ < 0 && horizontal != 0) {
        settingsTab_ = static_cast<SettingsTab>(cycle(static_cast<int>(settingsTab_), horizontal, settingsTabs.size()));
        return;
    }
    if (focus_ < 0 && (input.down || input.confirm)) {
        if (settingsTab_ == SettingsTab::Credits) {
            if (!credits_.empty()) focus_ = creditPage_;
        } else {
            focus_ = 0;
        }
        return;
    }

    if (settingsTab_ == SettingsTab::Controls) {
        if (input.click && (padTab.contains(cursor) || keysTab.contains(cursor))) {
            controllerBindings_ = padTab.contains(cursor);
            focus_ = 0;
            return;
        }
        constexpr int reset = actionCount + 1;
        if (input.reset || (input.click && defaultsButton.contains(cursor)) || (focus_ == reset && input.confirm)) {
            controls.resetBindings(controllerBindings_);
            return;
        }
        if (focus_ == 0 && (horizontal || input.confirm)) {
            controllerBindings_ = !controllerBindings_;
            return;
        }
        if (focus_ > 0 && focus_ < reset && horizontal) {
            if (horizontal > 0 && focus_ <= bindingRows) focus_ += bindingRows;
            if (horizontal < 0 && focus_ > bindingRows) focus_ -= bindingRows;
        }
        if (vertical && focus_ >= 0) {
            if (vertical < 0 && focus_ == bindingRows + 1) focus_ = 0;
            else if (vertical > 0 && focus_ == bindingRows) focus_ = reset;
            else focus_ = std::clamp(focus_ + vertical, -1, reset);
        }
        if (input.click) {
            for (int i = 0; i < actionCount; ++i) {
                if (!bindingBox(i).contains(cursor)) continue;
                focus_ = i + 1;
                capture_ = i;
                break;
            }
        } else if (input.confirm && focus_ > 0 && focus_ < reset) {
            capture_ = focus_ - 1;
        }
        if (capture_ >= 0) cancelHeld_ = 0.0f;
        return;
    }

    if (settingsTab_ == SettingsTab::Graphics) {
        if (input.reset || (input.click && defaultsButton.contains(cursor)) ||
            (focus_ == graphicsRows && input.confirm)) {
            graphics_ = {};
            saveGraphics();
            return;
        }
        if (vertical && focus_ >= 0) focus_ = std::clamp(focus_ + vertical, -1, graphicsRows);
        int step = horizontal ? horizontal : static_cast<int>(input.confirm);
        if (input.click) {
            step = 0;
            for (int i = 0; i < graphicsRows; ++i) {
                if (!graphicsBox(i).contains(cursor)) continue;
                focus_ = i;
                step = cursor.x >= 908 && cursor.x < 962 ? -1 : 1;
                break;
            }
        }
        if (!step || focus_ < 0 || focus_ >= graphicsRows) return;
        if (focus_ == 0) {
            graphics_.preset = static_cast<QualityLevel>(cycle(static_cast<int>(graphics_.preset), step, qualityLabels.size()));
        } else if (focus_ == 1) {
            graphics_.lod = static_cast<QualityLevel>(cycle(static_cast<int>(graphics_.lod), step, qualityLabels.size()));
        } else if (focus_ == 2) {
            graphics_.renderScale = cycle(graphics_.renderScale, step, scaleLabels.size());
        } else if (focus_ == 3) {
            graphics_.bloom = !graphics_.bloom;
        } else {
            graphics_.reflections = !graphics_.reflections;
        }
        saveGraphics();
        return;
    }

    if (credits_.empty()) return;
    const int last = static_cast<int>(credits_.size()) - 1;
    if (focus_ >= 0 && (vertical || horizontal)) focus_ = std::clamp(focus_ + vertical + horizontal, -1, last);
    if (input.click) {
        for (int i = 0; i <= last; ++i) {
            if (creditBox(i).contains(cursor)) focus_ = i;
        }
    }
    if (focus_ >= 0) creditPage_ = focus_;
}

void Menu::drawSettings(UiDraw& ui, const Controls& controls) const
{
    const bool pad = controls.usingGamepad();
    ui.text("Settings", {69, 67}, 72, ink, italic);
    for (int i = 0; i < static_cast<int>(settingsTabs.size()); ++i) {
        stamp(ui, settingsTabs[i], settingsTabBox(i), static_cast<int>(settingsTab_) == i);
    }
    if (focus_ < 0) {
        const auto box = settingsTabBox(static_cast<int>(settingsTab_));
        ui.rounded({box.x + 12, box.y + box.h - 5, box.w - 24, 2}, 1.0f, white);
        keyHint(ui, {592, 161}, pad ? "D-pad" : "Left / Right", "Switch tabs");
    }
    panel(ui, settingsPanel, 17);
    const auto drawRow = [&](UiBox row, bool focused) {
        ui.roundedGradient(row, 8, focused ? glm::vec4(0.09f, 0.16f, 0.27f, 1.0f) : dark, ink);
        if (focused) ui.rounded({row.x + 2, row.y + 7, 3, row.h - 14}, 1.5f, uiAccentBlue);
    };

    if (settingsTab_ == SettingsTab::Controls) {
        stamp(ui, "Gamepad", padTab, controllerBindings_);
        stamp(ui, "Keyboard", keysTab, !controllerBindings_);
        if (focus_ == 0) {
            const UiBox profile = controllerBindings_ ? padTab : keysTab;
            ui.line({profile.x + 11, 272}, {profile.x + profile.w - 11, 272}, 2, uiAccentBlue);
        }
        for (int i = 0; i < actionCount; ++i) {
            const UiBox row = bindingBox(i);
            const bool focused = focus_ == i + 1;
            drawRow(row, focused);
            ui.text(actionLabels[i], {row.x + 14, row.y + 6}, 21, focused ? white : muted);
            const auto binding = capture_ == i ? (controllerBindings_ ? "Move or press a control" : "Press a key")
                                               : controls.binding(i, controllerBindings_);
            const float width = ui.measure(binding, 17) + 22;
            stamp(ui, binding, {row.x + row.w - width - 8, row.y + 5, width, 26}, focused);
        }
        stamp(ui, "Reset defaults", defaultsButton, focus_ == actionCount + 1);
        if (capture_ >= 0) {
            ui.text(controllerBindings_ ? "Hold B to cancel" : "Select Cancel to close", {284, 630}, 21, muted);
        } else {
            ui.text(pad ? "A to rebind" : "Enter to rebind", {284, 630}, 21, muted);
        }
    } else if (settingsTab_ == SettingsTab::Graphics) {
        constexpr std::array titles{"Graphics preset", "Scenery LOD", "Render scale", "Bloom", "Track reflections"};
        constexpr std::array details{"Sets shadow quality, fog detail, and default scenery levels.",
            "Controls tree detail, grass density, and draw distance in Derelict Camp.",
            "Changes 3D resolution. Menus and HUD stay sharp.", "Adds a glow around bright lights.",
            "Reflects nearby track scenery in the car's clear coat."};
        const std::array values{qualityLabels[static_cast<int>(graphics_.preset)],
            graphics_.lod == QualityLevel::Automatic ? "Match preset" : qualityLabels[static_cast<int>(graphics_.lod)],
            scaleLabels[graphics_.renderScale], graphics_.bloom ? "On" : "Off", graphics_.reflections ? "On" : "Off"};
        for (int i = 0; i < graphicsRows; ++i) {
            const auto row = graphicsBox(i);
            drawRow(row, focus_ == i);
            ui.text(titles[i], {row.x + 20, row.y + 9}, 31, white, italic);
            ui.text(details[i], {row.x + 21, row.y + 42}, 18, muted);
            stamp(ui, "<", {908, row.y + 13, 39, 39}, false);
            centered(ui, values[i], {1036, row.y + row.h * 0.5f}, 22, pale);
            stamp(ui, ">", {1126, row.y + 13, 39, 39}, false);
        }
        stamp(ui, "Reset defaults", defaultsButton, focus_ == graphicsRows);
        ui.text("Changes apply immediately", {284, 630}, 21, muted);
    } else if (!credits_.empty()) {
        for (int i = 0; i < static_cast<int>(credits_.size()); ++i) {
            const auto row = creditBox(i);
            drawRow(row, focus_ == i);
            ui.text(credits_[i].front(), {row.x + 14, row.y + 11}, 22, creditPage_ == i ? white : muted);
        }
        ui.line({443, 240}, {443, 642}, 1, rail);
        const auto& section = credits_[creditPage_];
        ui.text(section.front(), {471, 238}, 45, white, italic);
        float y = 309.0f;
        for (std::size_t i = 1; i < section.size(); ++i) {
            const bool link = section[i].rfind("https://", 0) == 0;
            ui.text(section[i], {473, y}, link ? 17.0f : 19.0f, link ? pale : muted);
            y += 31.0f;
        }
    } else {
        ui.text("Audio credits unavailable", {96, 242}, 25, muted);
    }
}

void Menu::setGarageAsset(const CarAsset& asset)
{
    auto& config = setups_[static_cast<std::size_t>(asset.model)];
    const auto paint = std::find_if(asset.body.begin(), asset.body.end(), [](const MeshPart& part) {
        return part.surface.paint;
    });
    if (paint != asset.body.end()) {
        const auto linear = glm::clamp(paint->surface.color, 0.0f, 1.0f);
        const auto srgb = glm::mix(linear * 12.92f, 1.055f * glm::pow(linear, glm::vec3(1.0f / 2.4f)) - 0.055f,
                                   glm::greaterThan(linear, glm::vec3(0.0031308f)));
        stockPaint_ = glm::ivec3(glm::round(srgb * 255.0f));
    }
    for (std::size_t i = 0; i < garageParts.size(); ++i) {
        garageParts_[i] = std::any_of(asset.body.begin(), asset.body.end(), [&](const MeshPart& part) {
            return part.carPart == garageParts[i];
        });
        if (!garageParts_[i]) config.*garageToggles[i] = false;
    }
}

bool Menu::updateGarage(const MenuInput& input, glm::vec2 cursor, float dt, bool activated)
{
    auto& setup = setups_[static_cast<std::size_t>(selectedCar_)];
    if (input.reset) setup.paint = glm::ivec3(-1);
    if (!input.mouseHeld) dragPaint_ = -1;
    if (input.click && focus_ < 3 && paintSlider(option(focus_)).contains(cursor)) dragPaint_ = focus_;
    if (dragPaint_ >= 0) {
        if (setup.paint.x < 0) setup.paint = stockPaint_;
        const auto slider = paintSlider(option(dragPaint_));
        setup.paint[dragPaint_] = static_cast<int>(std::lround(255.0f * std::clamp((cursor.x - slider.x) / slider.w, 0.0f, 1.0f)));
        return true;
    }
    if (focus_ < 3) {
        if (!input.horizontalHeld) paintRepeat_ = 0.0f;
        else paintRepeat_ -= dt;
        if (input.left || input.right || (input.horizontalHeld && paintRepeat_ <= 0.0f)) {
            if (setup.paint.x < 0) setup.paint = stockPaint_;
            setup.paint[focus_] = std::clamp(setup.paint[focus_] + input.horizontalHeld, 0, 255);
            paintRepeat_ = input.left || input.right ? 0.32f : 0.018f;
        }
    } else if (focus_ < garageContinue && (activated || input.left || input.right) && garageParts_[focus_ - 3]) {
        auto& enabled = setup.*garageToggles[focus_ - 3];
        enabled = input.right || (!input.left && !enabled);
    }
    return focus_ != garageContinue;
}

void Menu::drawGarage(UiDraw& ui, bool pad) const
{
    const auto& config = setup(selectedCar_);
    const auto paint = config.paint.x < 0 ? stockPaint_ : config.paint;
    const auto& spec = powertrainSpec(selectedCar_, config.diesel);
    ui.text("Garage", {69, 113}, 75, ink, italic);
    panel(ui, {72, 199, 456, 347});
    for (int i = 0; i < garageContinue; ++i) {
        const UiBox row = option(i);
        const bool selected = i == focus_;
        ui.roundedGradient(row, 7, selected ? deepBlue : dark, ink);
        ui.text(garageLabels[i], {row.x + 12, row.y + 8}, 22, selected ? white : muted);
        if (i < 3) {
            const auto slider = paintSlider(row);
            glm::vec4 lo(glm::vec3(paint) / 255.0f, 1.0f);
            auto hi = lo;
            lo[i] = 0.0f;
            hi[i] = 1.0f;
            for (int step = 0; step < 39; ++step) {
                ui.rect({slider.x + step * 6.0f, slider.y, 6.0f, slider.h}, glm::mix(lo, hi, step / 38.0f));
            }
            const float x = slider.x + slider.w * paint[i] / 255.0f;
            ui.rect({x - 2, slider.y - 3, 4, slider.h + 6}, white);
            ui.text(std::to_string(paint[i]), {row.x + 379, row.y + 9}, 20, white);
        } else {
            const bool enabled = config.*garageToggles[i - 3];
            const char* value = !garageParts_[i - 3] ? "Unavailable" :
                                i == 6 ? (enabled ? "Turbo diesel" : "Gasoline") :
                                enabled ? (i == 3 ? "+3% power" : "Equipped") : "Off";
            ui.text(value, {row.x + row.w - 14 - ui.measure(value, 20), row.y + 9}, 20,
                    enabled ? pale : muted);
        }
    }
    button(ui, option(garageContinue), "Choose a map", focus_ == garageContinue, time_, Icon::Map);
    panel(ui, {829, 555, 371, 75});
    ui.rounded({846, 576, 34, 34}, 6, glm::vec4(glm::vec3(paint) / 255.0f, 1.0f));
    ui.text(spec.name, {893, 569}, 36, white, italic);
    const int hp = static_cast<int>(std::lround(horsepower(spec) * (config.hoodIntake ? 1.03f : 1.0f)));
    const auto power = std::to_string(hp) + " hp";
    ui.text(power, {1181 - ui.measure(power, 25, italic), 582}, 25, pale, italic);
    keyHint(ui, {72, 662}, pad ? "D-pad / Stick" : "Arrow keys", "Adjust");
    keyHint(ui, {272, 662}, pad ? "A" : "Enter", "Select");
    keyHint(ui, {403, 662}, pad ? "X" : "Delete", "Reset paint");
    keyHint(ui, {829, 662}, pad ? "Look controls" : "Drag preview", "Rotate");
}

SceneView Menu::scene(const UiDraw& ui, int width, int height) const
{
    SceneView view;
    view.course = course_;
    view.menu = screen_ != Screen::Drive && screen_ != Screen::Pause;
    view.showCar = screen_ != Screen::Settings && screen_ != Screen::Maps;
    view.titleDrift = screen_ == Screen::Splash;
    view.menuColor = colorReveal(time_);
    view.time = time_;
    const auto point = ui.toScreen(rippleOrigin_) / glm::vec2(width, height);
    view.ripple = {point.x, 1.0f - point.y, (time_ - rippleAt_) / 0.68f};
    return view;
}

CarModel Menu::displayedCar() const
{
    if (screen_ == Screen::Splash) return CarModel::SlimRider;
    if (screen_ == Screen::Main) {
        return hasGale_ && static_cast<int>((time_ - entered_) / 7.0f) % 2 == 1 ? CarModel::Gale : CarModel::SlimRider;
    }
    return selectedCar_;
}

void Menu::draw(UiDraw& ui, const Controls& controls) const
{
    if (screen_ == Screen::Drive) return;
    const bool pad = controls.usingGamepad();
    const auto model = displayedCar();
    const auto& spec = carSpec(model);
    if (screen_ == Screen::Pause) {
        ui.rect(ui.viewportBounds(), {0.021f, 0.021f, 0.024f, 0.68f});
        panel(ui, {396, 112, 488, 486}, 24);
        centered(ui, courseName(course_), {640, 148}, 19, muted);
        centered(ui, "Race paused", {640, 189}, 62, white, italic);
        constexpr std::array pause{"Resume", "Restart", "Settings", "Main menu"};
        constexpr std::array icons{Icon::Play, Icon::Restart, Icon::Controls, Icon::Exit};
        for (int i = 0; i < count(); ++i) {
            button(ui, option(i), pause[i], focus_ == i, time_, icons[i]);
        }
        centered(ui, pad ? "A  Confirm" : "Enter  Confirm", {640, 561}, 17, muted);
        stamp(ui, pad ? "B  Resume" : "Esc  Resume", backButton, false);
        return;
    }

    const float colors = colorReveal(time_);
    if (colors > 0.0f) ambientParticles(ui, time_, colors);
    const bool home = screen_ == Screen::Main || screen_ == Screen::Splash;
    if (!home) {
        stamp(ui, capture_ >= 0 ? "Cancel" : (pad ? "B  Back" : "Esc  Back"), backButton, false);
    }
    const float age = time_ - rippleAt_;
    const float press = pending_ != screen_ && age >= 0.0f ? std::max(0.0f, 1.0f - age * 5.0f) : 0.0f;

    if (home) {
        const bool splash = screen_ == Screen::Splash;
        logo(ui, splash ? 207.0f : 106.0f);
        if (splash) {
            button(ui, startButton, "Start", true, time_, Icon::Play, pad ? "Press A" : "Press Enter", press, true);
        } else {
            constexpr std::array names{"Play", "Multiplayer", "Settings"};
            constexpr std::array icons{Icon::Play, Icon::Players, Icon::Controls};
            for (int i = 0; i < count(); ++i) {
                UiBox box = option(i);
                const float enter = std::clamp(1.0f - (time_ - entered_ - i * 0.035f) * 4.5f, 0.0f, 1.0f);
                box.x -= enter * enter * 46.0f;
                button(ui, box, names[i], focus_ == i, time_, icons[i], i == 1 ? "Coming soon" : "", i == 0 ? press : 0.0f);
            }
            panel(ui, {854, 552, 346, 78});
            ui.text(spec.name, {880, 569}, 43, white, italic);
            ui.text(spec.rearDriveFraction == 1.0f ? "RWD" : "AWD", {1115, 580}, 26, muted, italic);
        }
    } else if (screen_ == Screen::Cars) {
        ui.text("Choose a car", {69, 116}, 75, ink, italic);
        for (int i = 0; i < count(); ++i) {
            const CarModel choice = hasGale_ && i == 0 ? CarModel::Gale : CarModel::SlimRider;
            button(ui, option(i), carSpec(choice).name, focus_ == i, time_, Icon::Car, manufacturer(choice));
        }
        driveSplit(ui, spec);
        panel(ui, {606, 543, 594, 81});
        ui.text(spec.name, {626, 560}, 45, white, italic);
        const std::array values{std::to_string(horsepower(spec)) + " hp", std::to_string(static_cast<int>(spec.mass)) + " kg",
                                std::string(spec.rearDriveFraction == 1.0f ? "RWD" : "AWD")};
        constexpr std::array labels{"POWER", "WEIGHT", "DRIVE"};
        for (int i = 0; i < 3; ++i) {
            const float x = 835.0f + 121.0f * i;
            ui.line({x - 14, 556}, {x - 14, 610}, 1, rail);
            ui.text(labels[i], {x, 557}, 15, muted);
            ui.text(values[i], {x, 578}, 31, white, italic);
        }
    } else if (screen_ == Screen::Garage) {
        drawGarage(ui, pad);
    } else if (screen_ == Screen::Maps) {
        ui.text("Choose a map", {69, 116}, 75, ink, italic);
        button(ui, option(0), courseName(Course::TestGrid), focus_ == 0, time_, Icon::Map, "Open practice surface");
        button(ui, option(1), courseName(Course::DustlineOval), focus_ == 1, time_, Icon::Oval, "Wide desert circuit");
        button(ui, option(2), courseName(Course::DerelictCamp), focus_ == 2, time_, Icon::Map, "Endless mountain road");
        drawCourse(ui, course_, time_);
        button(ui, goButton, course_ == Course::DerelictCamp ? "Start drive" : "Start race",
               true, time_, Icon::Play, pad ? "Press A" : "Press Enter", press, true);
        panel(ui, {72, 455, 456, 81});
        icon(ui, Icon::Car, {104, 496}, uiAccentBlue);
        ui.text(spec.name, {133, 477}, 39, white, italic);
    } else if (screen_ == Screen::Settings) {
        drawSettings(ui, controls);
    }
    if (screen_ == Screen::Main || screen_ == Screen::Cars || screen_ == Screen::Maps) {
        keyHint(ui, {72, 662}, pad ? "D-pad / Stick" : "Arrow keys", "Select");
        if (screen_ != Screen::Maps) keyHint(ui, {253, 662}, pad ? "A" : "Enter", "Confirm");
    }
    if (age >= 0.0f && age < 0.55f) {
        const float flash = std::max(0.0f, 1.0f - age / 0.55f);
        ui.arc(rippleOrigin_, glm::vec2(14.0f + age * 810.0f), 0, pi * 2.0f, 3.5f * flash,
               {0.86f, 1.0f, 1.0f, 0.58f * flash});
        transitionParticles(ui, rippleOrigin_, age);
    }
    const float arrival = std::clamp((time_ - entered_) / 0.28f, 0.0f, 1.0f);
    if (arrival < 1.0f) {
        const float fade = (1.0f - arrival) * (1.0f - arrival);
        ui.rect(ui.viewportBounds(), opacity(white, fade * 0.60f));
    }
}
