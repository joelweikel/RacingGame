#include "menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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
constexpr UiBox padTab{72, 168, 205, 40};
constexpr UiBox keysTab{285, 168, 211, 40};
constexpr UiBox defaultsButton{72, 626, 169, 38};
constexpr UiBox goButton{842, 560, 358, 70};
constexpr UiBox startButton{72, 488, 340, 72};

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

void controllerDiagram(UiDraw& ui, std::string_view active)
{
    // Top sticks, lower D-pad and face buttons match the Wii U Pro Controller layout.
    constexpr std::array<glm::vec2, 20> shell{{
        {735, 306}, {755, 272}, {812, 252}, {1036, 252}, {1095, 268},
        {1127, 303}, {1147, 391}, {1147, 453}, {1120, 490}, {1083, 478},
        {1044, 423}, {1007, 405}, {855, 405}, {824, 426}, {795, 480},
        {752, 488}, {724, 456}, {723, 407}, {727, 366}, {731, 332}
    }};
    std::vector<glm::vec2> outline;
    for (std::size_t i = 0; i < shell.size(); ++i) {
        const auto a = shell[(i + shell.size() - 1) % shell.size()], b = shell[i];
        const auto c = shell[(i + 1) % shell.size()], d = shell[(i + 2) % shell.size()];
        for (int j = 0; j < 6; ++j) {
            const float t = j / 6.0f;
            outline.push_back(0.5f * (2.0f * b + t * (-a + c + t * (2.0f*a - 5.0f*b + 4.0f*c - d +
                               t * (-a + 3.0f*b - 3.0f*c + d)))));
        }
    }
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const auto a = outline[i], b = outline[(i + 1) % outline.size()];
        ui.poly({{935, 355}, a, b}, ink);
        ui.line(a, b, 2.5f, rim);
    }
    const auto padButton = [&](std::string_view label, glm::vec2 at, float r) {
        const bool highlighted = label == active;
        ui.circle(at, r + 2, highlighted ? uiAccentBlue : rim);
        ui.circle(at, r, highlighted ? deepBlue : dark);
        centered(ui, label, at, r > 13 ? 16.0f : 12.0f, white);
    };
    const auto stick = [&](glm::vec2 at, bool left) {
        const bool hot = active.rfind(left ? "L Stick" : "R Stick", 0) == 0;
        ui.circle(at, 33, hot ? uiAccentBlue : rim);
        ui.circle(at, 30, ink);
        ui.circle(at, 22, hot ? deepBlue : dark);
        ui.arc(at, {16, 16}, 0, 2 * pi, 1, hot ? white : muted);
    };
    stick({823, 300}, true);
    stick({1042, 300}, false);
    const auto dp = active.rfind("D-Pad", 0) == 0 ? uiAccentBlue : pale;
    ui.rounded({799, 354, 49, 16}, 4, dp);
    ui.rounded({816, 337, 16, 49}, 4, dp);
    ui.circle({824, 362}, 4, dark);
    padButton("X", {1042, 343}, 12);
    padButton("A", {1063, 364}, 12);
    padButton("B", {1042, 385}, 12);
    padButton("Y", {1021, 364}, 12);
    padButton("-", {901, 328}, 9);
    padButton("+", {966, 328}, 9);
    padButton("H", {934, 351}, 12);
    stamp(ui, "L", {766, 245, 54, 22}, active == "L");
    stamp(ui, "ZL", {822, 241, 50, 22}, active == "ZL");
    stamp(ui, "ZR", {990, 241, 50, 22}, active == "ZR");
    stamp(ui, "R", {1043, 245, 54, 22}, active == "R");
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

std::string_view courseName(Course course) { return course == Course::TestGrid ? "Test Grid" : "Dustline Oval"; }

Menu::Menu(CarModel initialModel, bool hasGale) : selectedCar_(initialModel), hasGale_(hasGale) {}

int Menu::count() const
{
    switch (screen_) {
    case Screen::Main: return 3;
    case Screen::Cars: return hasGale_ ? 2 : 1;
    case Screen::Maps: return 2;
    case Screen::Settings: return actionCount;
    case Screen::Pause: return 4;
    default: return 0;
    }
}

int Menu::firstBinding() const { return std::clamp(focus_ - 4, 0, actionCount - 8); }

UiBox Menu::option(int index) const
{
    switch (screen_) {
    case Screen::Main: return {72, 352.0f + index * 89.0f, 456, 73};
    case Screen::Cars: return {72, 223.0f + index * 97.0f, 456, 73};
    case Screen::Maps: return {72, 218.0f + index * 97.0f, 456, 73};
    case Screen::Settings: return {82, 226.0f + (index - firstBinding()) * 46.0f, 414, 39};
    case Screen::Pause: return {424, 232.0f + index * 74.0f, 432, 60};
    default: return {};
    }
}

void Menu::open(Screen next)
{
    screen_ = pending_ = next;
    entered_ = time_;
    capture_ = -1;
    focus_ = next == Screen::Cars && hasGale_ ? (selectedCar_ == CarModel::Gale ? 0 : 1) :
             next == Screen::Maps && course_ == Course::DustlineOval ? 1 : 0;
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
        case Screen::Maps: open(Screen::Cars); break;
        case Screen::Pause: open(Screen::Drive); break;
        case Screen::Main: open(Screen::Splash); break;
        default: break;
        }
        return;
    }

    if (screen_ == Screen::Settings) {
        if (input.left || input.right || (input.click && (padTab.contains(cursor) || keysTab.contains(cursor)))) {
            controllerBindings_ = input.click ? padTab.contains(cursor) : !controllerBindings_;
            return;
        }
        if (input.reset || (input.click && defaultsButton.contains(cursor))) {
            controls.resetBindings(controllerBindings_);
            return;
        }
    }
    const int size = count();
    const int move = static_cast<int>(input.down || (screen_ != Screen::Settings && input.right)) -
                     static_cast<int>(input.up || (screen_ != Screen::Settings && input.left));
    if (move != 0 && size > 0) focus_ = (focus_ + move + size) % size;

    bool activated = input.confirm;
    if (input.click || (input.mouseMoved && screen_ != Screen::Settings)) {
        const int first = screen_ == Screen::Settings ? firstBinding() : 0;
        const int last = screen_ == Screen::Settings ? first + 8 : size;
        for (int i = first; i < last; ++i) {
            if (option(i).contains(cursor)) {
                focus_ = i;
                activated |= input.click && screen_ != Screen::Maps;
                break;
            }
        }
    }
    if (screen_ == Screen::Cars) selectedCar_ = hasGale_ && focus_ == 0 ? CarModel::Gale : CarModel::SlimRider;
    if (screen_ == Screen::Maps) {
        course_ = focus_ == 0 ? Course::TestGrid : Course::DustlineOval;
        if (input.click && goButton.contains(cursor)) activated = true;
    }
    if (!activated) return;

    switch (screen_) {
    case Screen::Main:
        if (focus_ == 0) transition(Screen::Cars, option(0).center());
        if (focus_ == 2) { settingsReturn_ = Screen::Main; open(Screen::Settings); }
        break;
    case Screen::Cars: open(Screen::Maps); break;
    case Screen::Maps: transition(Screen::Drive, goButton.center()); break;
    case Screen::Settings: capture_ = focus_; cancelHeld_ = 0.0f; break;
    case Screen::Pause:
        if (focus_ == 0) open(Screen::Drive);
        if (focus_ == 1) { startingRace_ = true; open(Screen::Drive); }
        if (focus_ == 2) { settingsReturn_ = Screen::Pause; open(Screen::Settings); }
        if (focus_ == 3) open(Screen::Main);
        break;
    default: break;
    }
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
    } else if (screen_ == Screen::Maps) {
        ui.text("Choose a map", {69, 116}, 75, ink, italic);
        button(ui, option(0), courseName(Course::TestGrid), focus_ == 0, time_, Icon::Map, "Open practice surface");
        button(ui, option(1), courseName(Course::DustlineOval), focus_ == 1, time_, Icon::Oval, "Wide desert circuit");
        drawCourse(ui, course_, time_);
        button(ui, goButton, "Start race", true, time_, Icon::Play, pad ? "Press A" : "Press Enter", press, true);
        panel(ui, {72, 455, 456, 81});
        icon(ui, Icon::Car, {104, 496}, uiAccentBlue);
        ui.text(spec.name, {133, 477}, 39, white, italic);
    } else if (screen_ == Screen::Settings) {
        ui.text("Controls", {69, 104}, 65, ink, italic);
        stamp(ui, "Gamepad", padTab, controllerBindings_);
        stamp(ui, "Keyboard", keysTab, !controllerBindings_);
        panel(ui, {72, 217, 454, 379});
        const int first = firstBinding();
        for (int i = first; i < first + 8; ++i) {
            const UiBox row = option(i);
            const bool focused = focus_ == i;
            ui.roundedGradient(row, 7, focused ? glm::vec4(0.09f, 0.16f, 0.27f, 1.0f) : dark, ink);
            if (focused) {
                ui.rounded({row.x + 2, row.y + 6, 3, row.h - 12}, 1.5f, uiAccentBlue);
                ui.bloom({row.x + 1, row.y + 6, 6, row.h - 12}, opacity(uiAccentBlue, 0.43f));
            }
            ui.text(actionLabels[i], {row.x + 13, row.y + 6}, 23, focused ? white : muted);
            const auto binding = capture_ == i ? (controllerBindings_ ? "Move or press a control" : "Press a key")
                                               : controls.binding(i, controllerBindings_);
            const float width = ui.measure(binding, 17) + 20;
            stamp(ui, binding, {row.x + row.w - width - 9, row.y + 6, width, 26}, focused);
        }
        ui.rect({510, 226, 3, 361}, opacity(pale, 0.21f));
        ui.rect({510, 226.0f + first * 22.5f, 3, 181}, uiAccentBlue);
        stamp(ui, "Reset defaults", defaultsButton, false);
        if (capture_ >= 0) {
            ui.text(controllerBindings_ ? "Hold B to cancel" : "Select Cancel to close", {269, 634}, 19, ink);
        } else {
            keyHint(ui, {269, 634}, pad ? "A" : "Enter", "Rebind");
            keyHint(ui, {386, 634}, "Left / Right", "Switch profile");
        }
        panel(ui, {566, 168, 634, 428});
        ui.text(controllerBindings_ ? "Wii U Pro Controller" : "Keyboard driving controls", {590, 181}, 38, white, italic);
        ui.line({591, 228}, {1175, 228}, 1, rail);
        if (controllerBindings_) {
            controllerDiagram(ui, controls.binding(focus_, true));
        } else {
            constexpr std::array actions{Action::Accelerate, Action::Brake, Action::Left, Action::Right};
            for (std::size_t i = 0; i < actions.size(); ++i) {
                const int action = static_cast<int>(actions[i]);
                const UiBox tile{618.0f + (i % 2) * 274.0f, 264.0f + (i / 2) * 112.0f, 252, 90};
                ui.roundedGradient(tile, 11, ink, dark);
                centered(ui, actionLabels[action], {tile.center().x, tile.y + 20}, 19, muted);
                stamp(ui, controls.binding(action, false), {tile.x + 42, tile.y + 38, 168, 38}, action == focus_);
            }
        }
        ui.line({591, 513}, {1175, 513}, 1, rail);
        ui.text(actionLabels[focus_], {591, 528}, 29, white, italic);
        const auto activeBinding = controls.binding(focus_, controllerBindings_);
        const float keyWidth = ui.measure(activeBinding, 17) + 34;
        stamp(ui, activeBinding, {1174 - keyWidth, 531, keyWidth, 32}, true);
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
