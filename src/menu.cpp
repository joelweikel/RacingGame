#include "menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr glm::vec4 ink{0.057f, 0.14f, 0.23f, 1.0f};
constexpr glm::vec4 dark{0.10f, 0.24f, 0.37f, 1.0f};
constexpr glm::vec4 muted{0.29f, 0.40f, 0.50f, 1.0f};
constexpr glm::vec4 white{0.97f, 0.995f, 1.0f, 1.0f};
constexpr glm::vec4 pale{0.83f, 0.93f, 0.97f, 1.0f};
constexpr glm::vec4 teal{0.33f, 0.92f, 0.90f, 1.0f};
constexpr glm::vec4 coral{0.99f, 0.49f, 0.34f, 1.0f};
constexpr float pi = 3.141592654f;
constexpr auto italic = UiFont::Display;
constexpr UiBox backButton{1114, 42, 99, 36};
constexpr UiBox padTab{73, 163, 208, 42};
constexpr UiBox keysTab{287, 163, 210, 42};
constexpr UiBox defaultsButton{73, 629, 200, 38};
constexpr UiBox goButton{858, 554, 342, 61};

std::string_view manufacturer(CarModel model)
{
    return model == CarModel::Gale ? "Joeltech" : "Slim Motorsports";
}

void centered(UiDraw& ui, std::string_view name, glm::vec2 center, float size, glm::vec4 color, UiFont font = UiFont::Label)
{
    ui.text(name, {center.x - ui.measure(name, size, font) * 0.5f, center.y - size * 0.49f}, size, color, font);
}

void stamp(UiDraw& ui, std::string_view label, UiBox b, bool selected)
{
    ui.rounded(b, 15, selected ? ink : white);
    centered(ui, label, b.center(), 17, selected ? white : ink);
}

void panel(UiDraw& ui, UiBox b, float radius = 20.0f)
{
    ui.rounded({b.x, b.y + 6, b.w, b.h}, radius, {0.04f, 0.19f, 0.30f, 0.17f});
    ui.rounded(b, radius, {0.96f, 0.99f, 1.0f, 0.94f});
}

void button(UiDraw& ui, UiBox box, std::string_view text, bool focused, float time,
            std::string_view description = {}, float press = 0.0f)
{
    box.x += press * 5.0f;
    box.y += press * 3.0f;
    box.w -= press * 10.0f;
    box.h -= press * 6.0f;
    const float halo = 0.40f + 0.15f * std::sin(time * 6.0f);
    if (focused) {
        ui.rounded({box.x - 5, box.y - 5, box.w + 10, box.h + 10}, 19, {0.24f, 0.93f, 0.90f, halo});
    }
    ui.rounded({box.x, box.y + 4, box.w, box.h}, 15, {0.04f, 0.18f, 0.28f, 0.25f});
    ui.rounded(box, 15, focused ? ink : white);
    ui.rounded({box.x + 2, box.y + 2, box.w - 4, 24}, 12,
               focused ? glm::vec4(0.35f, 0.66f, 0.82f, 0.10f) : glm::vec4(0.66f, 0.86f, 0.94f, 0.21f));
    ui.rounded({box.x + 13, box.y + 13, 5, box.h - 26}, 2.5f, focused ? teal : pale);
    ui.text(text, {box.x + 31, box.y + 8}, 34, focused ? white : ink, italic);
    if (!description.empty()) {
        const float w = ui.measure(description, 15);
        ui.text(description, {box.x + box.w - w - 19, box.y + (box.h - 18) * 0.5f},
                15, focused ? teal : muted);
    } else {
        const glm::vec2 edge{box.x + box.w - 23 + (focused ? 2.0f * std::sin(time * 7) : 0.0f), box.center().y};
        const auto color = focused ? teal : dark;
        ui.line(edge - glm::vec2(6, 6), edge, 2, color);
        ui.line(edge, edge - glm::vec2(6, -6), 2, color);
    }
}

void mark(UiDraw& ui, glm::vec2 top, float scale)
{
    for (int i = 0; i < 3; ++i) {
        const auto p = top + glm::vec2(13.0f * i * scale, 0.0f);
        ui.poly({p + glm::vec2(8, 0) * scale, p + glm::vec2(17, 0) * scale,
                 p + glm::vec2(8, 19) * scale, p + glm::vec2(-1, 19) * scale}, i == 2 ? coral : teal);
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

void controllerDiagram(UiDraw& ui, std::string_view active)
{
    // Top sticks, lower D-pad and face buttons match the Wii U Pro Controller layout.
    constexpr std::array<glm::vec2, 20> rim{{
        {735, 306}, {755, 272}, {812, 252}, {1036, 252}, {1095, 268},
        {1127, 303}, {1147, 391}, {1147, 453}, {1120, 490}, {1083, 478},
        {1044, 423}, {1007, 405}, {855, 405}, {824, 426}, {795, 480},
        {752, 488}, {724, 456}, {723, 407}, {727, 366}, {731, 332}
    }};
    std::vector<glm::vec2> outline;
    for (std::size_t i = 0; i < rim.size(); ++i) {
        const auto a = rim[(i + rim.size() - 1) % rim.size()], b = rim[i];
        const auto c = rim[(i + 1) % rim.size()], d = rim[(i + 2) % rim.size()];
        for (int j = 0; j < 6; ++j) {
            const float t = j / 6.0f;
            outline.push_back(0.5f * (2.0f * b + t * (-a + c + t * (2.0f*a - 5.0f*b + 4.0f*c - d +
                               t * (-a + 3.0f*b - 3.0f*c + d)))));
        }
    }
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const auto a = outline[i], b = outline[(i + 1) % outline.size()];
        ui.poly({{935, 355}, a, b}, ink);
        ui.line(a, b, 2.5f, {0.26f, 0.52f, 0.65f, 1.0f});
    }
    const auto padButton = [&](std::string_view label, glm::vec2 at, float r) {
        const bool highlighted = label == active;
        ui.circle(at, r + 2, highlighted ? teal : glm::vec4(0.23f, 0.43f, 0.57f, 1.0f));
        ui.circle(at, r, highlighted ? teal : dark);
        centered(ui, label, at, r > 13 ? 16.0f : 12.0f, highlighted ? ink : white);
    };
    const auto stick = [&](glm::vec2 at, bool left) {
        const bool hot = active.rfind(left ? "L Stick" : "R Stick", 0) == 0;
        ui.circle(at, 33, hot ? teal : glm::vec4(0.31f, 0.52f, 0.63f, 1));
        ui.circle(at, 30, {0.05f, 0.12f, 0.20f, 1.0f});
        ui.circle(at, 22, hot ? teal : glm::vec4(0.17f, 0.31f, 0.42f, 1));
        ui.arc(at, {16, 16}, 0, 2 * pi, 1, hot ? ink : muted);
    };
    stick({823, 300}, true);
    stick({1042, 300}, false);
    const auto dp = active.rfind("D-Pad", 0) == 0 ? teal : pale;
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
    const UiBox bounds{646, 173, 554, 362};
    panel(ui, bounds, 23);
    ui.rounded({657, 184, 532, 340}, 18, oval ? glm::vec4(0.65f, 0.40f, 0.27f, 1) : dark);
    const auto line = oval ? glm::vec4(0.98f, 0.76f, 0.54f, 0.17f) : glm::vec4(0.63f, 0.86f, 0.94f, 0.24f);
    for (int x = 677; x < 1180; x += 25) ui.line({x, 198}, {x, 509}, 1, line);
    for (int y = 201; y < 517; y += 25) ui.line({675, y}, {1174, y}, 1, line);
    if (oval) {
        const auto route = [&](float width, glm::vec4 fill) {
            ui.line({811, 265}, {1033, 265}, width, fill);
            ui.line({811, 443}, {1033, 443}, width, fill);
            ui.arc({811, 354}, {85, 89}, pi * 0.5f, pi * 1.5f, width, fill);
            ui.arc({1033, 354}, {85, 89}, -pi * 0.5f, pi * 0.5f, width, fill);
        };
        route(30, white);
        route(23, ink);
        route(2.5f, coral);
        ui.circle({848.0f + 155.0f * std::fmod(time * 0.23f, 1.0f), 443}, 6, teal);
    } else {
        ui.circle({923, 354}, 39, {0.34f, 0.97f, 0.94f, 0.17f});
        for (float radius : {39.0f, 82.0f, 119.0f}) ui.arc({923, 354}, {radius, radius}, 0, pi * 2, 1.6f, line * 2.5f);
        ui.line({923, 272}, {923, 436}, 1, teal);
        ui.line({841, 354}, {1005, 354}, 1, teal);
        ui.circle({923, 354}, 7.0f + std::sin(time * 4.0f) * 2.0f, teal);
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
    case Screen::Main: return {72, 365.0f + index * 72.0f, 395, 55};
    case Screen::Cars: return {72, 217.0f + index * 117.0f, 420, 57};
    case Screen::Maps: return {72, 201.0f + index * 121.0f, 468, 59};
    case Screen::Settings: return {73, 223.0f + (index - firstBinding()) * 46.0f, 424, 39};
    case Screen::Pause: return {432, 262.0f + index * 68.0f, 416, 54};
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
        if (input.confirm || input.click || controls.justPressed(Action::Pause)) transition(Screen::Main, {165, 555});
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
    view.time = time_;
    const auto point = ui.toScreen(rippleOrigin_) / glm::vec2(width, height);
    view.ripple = {point.x, 1.0f - point.y, (time_ - rippleAt_) / 0.68f};
    return view;
}

CarModel Menu::displayedCar() const
{
    if (screen_ == Screen::Splash || screen_ == Screen::Main) {
        return hasGale_ && static_cast<int>((time_ - entered_) / 7.0f) % 2 == 1 ? CarModel::Gale : CarModel::SlimRider;
    }
    return selectedCar_;
}

void Menu::draw(UiDraw& ui, const Controls& controls) const
{
    const bool pad = controls.usingGamepad();
    const auto& spec = carSpec(displayedCar());
    if (screen_ == Screen::Drive) return;
    if (screen_ == Screen::Pause) {
        ui.rect({-1280, -720, 3840, 2160}, {0.035f, 0.095f, 0.16f, 0.70f});
        panel(ui, {394, 121, 492, 447}, 24);
        centered(ui, "Race paused", {640, 195}, 61, ink, italic);
        constexpr std::array pause{"Resume", "Restart", "Settings", "Main menu"};
        for (int i = 0; i < count(); ++i) button(ui, option(i), pause[i], focus_ == i, time_);
        stamp(ui, pad ? "B  Back" : "Esc  Back", backButton, false);
        return;
    }

    mark(ui, {72, 55}, 0.96f);
    if (screen_ != Screen::Main && screen_ != Screen::Splash) {
        ui.text("RACING GAME", {121, 51}, 25, ink, italic);
    }
    if (screen_ != Screen::Main && screen_ != Screen::Splash) {
        stamp(ui, capture_ >= 0 ? "Cancel" : (pad ? "B  Back" : "Esc  Back"), backButton, false);
    }
    ui.line({72, 96}, {1200, 96}, 1, {0.84f, 0.97f, 1.0f, 0.63f});
    const float age = time_ - rippleAt_;
    const float press = pending_ != screen_ && age >= 0.0f ? std::max(0.0f, 1.0f - age * 5.0f) : 0.0f;

    if (screen_ == Screen::Splash || screen_ == Screen::Main) {
        const bool splash = screen_ == Screen::Splash;
        const float heroY = splash ? 222.0f : 145.0f;
        ui.text("RACING", {64, heroY}, 128, {0.21f, 0.43f, 0.52f, 0.21f}, italic);
        ui.text("RACING", {60, heroY - 5}, 128, ink, italic);
        ui.text("GAME", {69, heroY + 96}, 110, dark, italic);
        ui.poly({{72, heroY + 192}, {303, heroY + 192}, {298, heroY + 200}, {72, heroY + 200}}, teal);
        if (splash) {
            UiBox start{73, 520, 273, 55};
            button(ui, start, pad ? "Press A to start" : "Press Enter to start", true, time_, {}, press);
        } else {
            constexpr std::array names{"Play", "Multiplayer", "Settings"};
            for (int i = 0; i < count(); ++i) {
                UiBox box = option(i);
                const float enter = std::clamp(1.0f - (time_ - entered_ - i * 0.035f) * 4.5f, 0.0f, 1.0f);
                box.x -= enter * enter * 46.0f;
                button(ui, box, names[i], focus_ == i, time_, i == 1 ? "Coming soon" : "", i == 0 ? press : 0.0f);
            }
        }
        panel(ui, {846, 550, 350, 88}, 18);
        ui.text(manufacturer(displayedCar()), {867, 564}, 19, muted);
        ui.text(spec.name, {864, 582}, 43, ink, italic);
        ui.rect({1192, 573, 4, 43}, teal);
    } else if (screen_ == Screen::Cars) {
        ui.text("Choose a car", {67, 110}, 75, ink, italic);
        for (int i = 0; i < count(); ++i) {
            const CarModel model = hasGale_ && i == 0 ? CarModel::Gale : CarModel::SlimRider;
            ui.text(manufacturer(model), {74, option(i).y - 26}, 19, ink);
            button(ui, option(i), carSpec(model).name, focus_ == i, time_);
        }
        panel(ui, {624, 537, 576, 123});
        ui.text(manufacturer(selectedCar_), {648, 549}, 20, muted);
        ui.text(spec.name, {646, 571}, 44, ink, italic);
        const std::array values{std::to_string(horsepower(spec)) + " hp", std::to_string(static_cast<int>(spec.mass)) + " kg",
                                std::string(spec.rearDriveFraction == 1.0f ? "RWD" : "AWD")};
        constexpr std::array labels{"POWER", "WEIGHT", "DRIVE"};
        for (int i = 0; i < 3; ++i) {
            const float x = 822.0f + 121.0f * i;
            ui.line({x - 14, 564}, {x - 14, 633}, 1, pale);
            ui.text(labels[i], {x, 570}, 14, muted);
            ui.text(values[i], {x, 593}, 31, ink, italic);
        }
    } else if (screen_ == Screen::Maps) {
        ui.text("Choose a map", {67, 109}, 75, ink, italic);
        button(ui, option(0), "Test Grid", focus_ == 0, time_);
        button(ui, option(1), "Dustline Oval", focus_ == 1, time_);
        ui.text("Open practice surface", {102, 267}, 20, ink);
        ui.text("Wide desert circuit", {102, 388}, 20, ink);
        drawCourse(ui, course_, time_);
        button(ui, goButton, "Go", true, time_, pad ? "A  START" : "ENTER  START", press);
        ui.text(spec.name, {74, 536}, 27, ink, italic);
    } else if (screen_ == Screen::Settings) {
        ui.text("Controls", {67, 101}, 64, ink, italic);
        stamp(ui, "Gamepad", padTab, controllerBindings_);
        stamp(ui, "Keyboard", keysTab, !controllerBindings_);
        panel(ui, {63, 213, 447, 380}, 16);
        const int first = firstBinding();
        for (int i = first; i < first + 8; ++i) {
            const UiBox row = option(i);
            ui.rounded(row, 10, focus_ == i ? ink : pale);
            ui.text(actionLabels[i], {row.x + 14, row.y + 8}, 21, focus_ == i ? white : ink);
            const auto binding = capture_ == i ? (controllerBindings_ ? "Move or press a control" : "Press a key") :
                                                controls.binding(i, controllerBindings_);
            const float w = ui.measure(binding, 17);
            ui.text(binding, {row.x + row.w - w - 14, row.y + 10}, 17, focus_ == i ? teal : muted);
        }
        ui.rounded({518, 221, 4, 363}, 2, {0.05f, 0.24f, 0.37f, 0.22f});
        ui.rounded({518, 221.0f + first * 22.5f, 4, 181}, 2, dark);
        stamp(ui, "Reset defaults", defaultsButton, false);
        if (capture_ >= 0) {
            ui.text(controllerBindings_ ? "Hold B to cancel" : "Select Cancel to close", {284, 637}, 16, ink);
        } else {
            ui.text(pad ? "A  Rebind     Left / Right  Profile" : "Enter  Rebind     Left / Right  Profile",
                    {284, 637}, 16, ink);
        }
        panel(ui, {566, 163, 634, 451}, 23);
        ui.text("Wii U Pro Controller", {590, 185}, 38, ink, italic);
        controllerDiagram(ui, controls.binding(focus_, true));
        ui.line({591, 538}, {1175, 538}, 1, pale);
        ui.text(actionLabels[focus_], {591, 554}, 26, ink, italic);
        ui.text(controls.binding(focus_, controllerBindings_), {592, 580}, 17, muted);
    }
    if (screen_ == Screen::Cars || screen_ == Screen::Maps) {
        ui.text(pad ? "D-pad / Stick   Select        A   Confirm" : "Arrow keys   Select        Enter   Confirm",
                {74, 653}, 17, ink);
    }
    if (age >= 0.0f && age < 0.55f) {
        const float flash = std::max(0.0f, 1.0f - age / 0.55f);
        ui.arc(rippleOrigin_, glm::vec2(14.0f + age * 810.0f), 0, pi * 2.0f, 3.5f * flash,
               {0.86f, 1.0f, 1.0f, 0.58f * flash});
    }
    const float arrival = std::clamp((time_ - entered_) / 0.28f, 0.0f, 1.0f);
    if (arrival < 1.0f) {
        const float fade = (1.0f - arrival) * (1.0f - arrival);
        ui.rect({-1280, -720, 3840, 2160}, {0.64f, 0.90f, 0.98f, fade * 0.85f});
    }
}
