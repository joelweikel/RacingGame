#include "controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

EM_JS(void, readControlPreferences, (char* out, int capacity), {
    try { stringToUTF8(localStorage.getItem("racinggame.controls.v1") || "", out, capacity); } catch (_) {}
});
EM_JS(void, writeControlPreferences, (const char* data), {
    try { localStorage.setItem('racinggame.controls.v1', UTF8ToString(data)); } catch (_) {}
});
#endif

namespace {

constexpr int axisBase = 32;
constexpr int axis(int n, bool positive) { return axisBase + n * 2 + static_cast<int>(positive); }
constexpr int endAxis = axis(GLFW_GAMEPAD_AXIS_LAST, true);
constexpr std::array<int, actionCount> defaultKeys{
    GLFW_KEY_W, GLFW_KEY_S, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_SPACE, GLFW_KEY_Q, GLFW_KEY_E,
    GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_R, GLFW_KEY_ESCAPE, GLFW_KEY_J, GLFW_KEY_L, GLFW_KEY_I, GLFW_KEY_K
};
constexpr std::array<int, actionCount> defaultPad{
    axis(GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, true), axis(GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, true),
    axis(GLFW_GAMEPAD_AXIS_LEFT_X, false), axis(GLFW_GAMEPAD_AXIS_LEFT_X, true), GLFW_GAMEPAD_BUTTON_X, GLFW_GAMEPAD_BUTTON_A,
    GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, GLFW_GAMEPAD_BUTTON_BACK,
    GLFW_GAMEPAD_BUTTON_Y, GLFW_GAMEPAD_BUTTON_B, GLFW_GAMEPAD_BUTTON_START,
    axis(GLFW_GAMEPAD_AXIS_RIGHT_X, false), axis(GLFW_GAMEPAD_AXIS_RIGHT_X, true),
    axis(GLFW_GAMEPAD_AXIS_RIGHT_Y, false), axis(GLFW_GAMEPAD_AXIS_RIGHT_Y, true)
};

#ifndef __EMSCRIPTEN__
std::filesystem::path bindingsPath()
{
#ifdef _WIN32
    if (const wchar_t* base = _wgetenv(L"LOCALAPPDATA")) {
        return std::filesystem::path(base) / "RacingGame" / "controls.cfg";
    }
#else
    if (const char* base = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(base) / "RacingGame" / "controls.cfg";
    }
    if (const char* base = std::getenv("HOME")) {
        return std::filesystem::path(base) / ".config" / "RacingGame" / "controls.cfg";
    }
#endif
    return "racing-controls.cfg";
}
#endif

bool down(GLFWwindow* window, int key) { return glfwGetKey(window, key) == GLFW_PRESS; }
int index(Action action) { return static_cast<int>(action); }

std::string keyName(int key)
{
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return std::string(1, static_cast<char>(key));
    switch (key) {
    case GLFW_KEY_SPACE: return "Space";
    case GLFW_KEY_ESCAPE: return "Esc";
    case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
    case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
    case GLFW_KEY_ENTER: return "Enter";
    case GLFW_KEY_TAB: return "Tab";
    case GLFW_KEY_LEFT: return "Left Arrow";
    case GLFW_KEY_RIGHT: return "Right Arrow";
    case GLFW_KEY_UP: return "Up Arrow";
    case GLFW_KEY_DOWN: return "Down Arrow";
    case GLFW_KEY_LEFT_CONTROL: return "Left Ctrl";
    case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
    case GLFW_KEY_BACKSPACE: return "Backspace";
    default:
        if (const char* name = glfwGetKeyName(key, 0)) return name;
        return "Key " + std::to_string(key);
    }
}

} // namespace

Controls::Controls(GLFWwindow* window) : window_(window), keyboard_(defaultKeys), gamepad_(defaultPad)
{
    load();
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, [](GLFWwindow* win, int key, int, int event, int) {
        if (event == GLFW_PRESS && key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST) {
            static_cast<Controls*>(glfwGetWindowUserPointer(win))->pendingKey_ = key;
        }
    });
}

void Controls::load()
{
#ifdef __EMSCRIPTEN__
    char data[512]{};
    readControlPreferences(data, sizeof(data));
    std::istringstream stream(data);
#else
    std::ifstream stream(bindingsPath());
#endif
    auto keys = keyboard_;
    auto pad = gamepad_;
    for (auto& key : keys) {
        if (!(stream >> key) || key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST) return;
    }
    for (auto& code : pad) {
        if (!(stream >> code) || code < 0 || (code > GLFW_GAMEPAD_BUTTON_LAST && code < axisBase) || code > endAxis) return;
    }
    keyboard_ = keys;
    gamepad_ = pad;
}

void Controls::save() const
{
    std::ostringstream values;
    for (int key : keyboard_) values << key << ' ';
    for (int value : gamepad_) values << value << ' ';
#ifdef __EMSCRIPTEN__
    writeControlPreferences(values.str().c_str());
#else
    const auto path = bindingsPath();
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    file << values.str();
#endif
}

float Controls::gamepadValue(int code, const GLFWgamepadstate& state) const
{
    if (code < axisBase) return state.buttons[code] == GLFW_PRESS ? 1.0f : 0.0f;
    const int n = (code - axisBase) / 2;
    const float raw = state.axes[n];
    const bool positive = (code - axisBase) % 2 != 0;
    if (n >= GLFW_GAMEPAD_AXIS_LEFT_TRIGGER) {
        return positive ? std::clamp((raw + 1.0f) * 0.5f, 0.0f, 1.0f) : 0.0f;
    }
    return std::clamp(((positive ? raw : -raw) - 0.16f) / 0.84f, 0.0f, 1.0f);
}

void Controls::poll()
{
    key_ = pendingKey_;
    pendingKey_ = GLFW_KEY_UNKNOWN;
    oldPad_ = pad_;
    if (joystick_ < 0 || !glfwGetGamepadState(joystick_, &pad_)) {
        joystick_ = -1;
        pad_ = {};
        pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;
        pad_.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;
        for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id) {
            if (glfwGetGamepadState(id, &pad_)) {
                joystick_ = id;
                break;
            }
        }
        if (joystick_ < 0) {
            pad_ = {};
            pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;
            pad_.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;
            gamepadActive_ = false;
        }
    }
    if (key_ != GLFW_KEY_UNKNOWN) gamepadActive_ = false;
    for (int button = 0; joystick_ >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
        if (pad_.buttons[button] && !oldPad_.buttons[button]) gamepadActive_ = true;
    }
    for (int i = 0; joystick_ >= 0 && i <= GLFW_GAMEPAD_AXIS_LAST; ++i) {
        if (std::abs(pad_.axes[i] - oldPad_.axes[i]) > 0.48f) gamepadActive_ = true;
    }

    previousValues_ = values_;
    for (int i = 0; i < actionCount; ++i) {
        values_[i] = down(window_, keyboard_[i]) ? 1.0f : joystick_ >= 0 ? gamepadValue(gamepad_[i], pad_) : 0.0f;
    }
    // Arrow keys remain usable on the default keyboard driving layout.
    if (keyboard_ == defaultKeys) {
        if (down(window_, GLFW_KEY_UP)) values_[index(Action::Accelerate)] = 1.0f;
        if (down(window_, GLFW_KEY_DOWN)) values_[index(Action::Reverse)] = 1.0f;
        if (down(window_, GLFW_KEY_LEFT)) values_[index(Action::Left)] = 1.0f;
        if (down(window_, GLFW_KEY_RIGHT)) values_[index(Action::Right)] = 1.0f;
    }

    MenuInput held;
    const auto button = [&](int code) { return joystick_ >= 0 && pad_.buttons[code] == GLFW_PRESS; };
    const float x = pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_X], y = pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    held.up = down(window_, GLFW_KEY_UP) || button(GLFW_GAMEPAD_BUTTON_DPAD_UP) || y < -0.57f;
    held.down = down(window_, GLFW_KEY_DOWN) || button(GLFW_GAMEPAD_BUTTON_DPAD_DOWN) || y > 0.57f;
    held.left = down(window_, GLFW_KEY_LEFT) || button(GLFW_GAMEPAD_BUTTON_DPAD_LEFT) || x < -0.57f;
    held.right = down(window_, GLFW_KEY_RIGHT) || button(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 0.57f;
    held.confirm = down(window_, GLFW_KEY_ENTER) || button(GLFW_GAMEPAD_BUTTON_A);
    held.back = down(window_, GLFW_KEY_ESCAPE) || button(GLFW_GAMEPAD_BUTTON_B);
    held.reset = down(window_, GLFW_KEY_DELETE) || button(GLFW_GAMEPAD_BUTTON_X);
    held.click = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window_, &mx, &my);
    held.cursor = {mx, my};
    held.mouseMoved = glm::length(held.cursor - previousMenu_.cursor) > 1.0f;
    if (held.click && !previousMenu_.click) gamepadActive_ = false;
    menu_ = {held.up && !previousMenu_.up, held.down && !previousMenu_.down,
             held.left && !previousMenu_.left, held.right && !previousMenu_.right,
             held.confirm && !previousMenu_.confirm, held.back && !previousMenu_.back,
             held.reset && !previousMenu_.reset, held.click && !previousMenu_.click,
             held.mouseMoved, held.cursor};
    previousMenu_ = held;
}

DriveInput Controls::drive() const
{
    return {values_[index(Action::Accelerate)] - values_[index(Action::Reverse)],
            values_[index(Action::Left)] - values_[index(Action::Right)],
            values_[index(Action::Brake)] > 0.5f, values_[index(Action::Handbrake)] > 0.5f};
}

void Controls::setMouseLook(bool enabled)
{
    if (!enabled && mouseLook_ && glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    mouseLook_ = enabled;
}

bool Controls::gamepadBackHeld() const
{
    return joystick_ >= 0 && pad_.buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS;
}

glm::vec2 Controls::look() const
{
    return {values_[index(Action::LookRight)] - values_[index(Action::LookLeft)],
            values_[index(Action::LookDown)] - values_[index(Action::LookUp)]};
}

bool Controls::justPressed(Action action) const
{
    const int i = index(action);
    return values_[i] > 0.55f && previousValues_[i] <= 0.55f;
}

std::string Controls::binding(int action, bool controller) const
{
    if (!controller) return keyName(keyboard_[action]);
    constexpr std::array<const char*, 15> names{
        "A", "B", "X", "Y", "L", "R", "-", "+", "Home", "L Stick Click", "R Stick Click",
        "D-Pad Up", "D-Pad Right", "D-Pad Down", "D-Pad Left"
    };
    constexpr std::array<const char*, 12> axes{
        "L Stick Left", "L Stick Right", "L Stick Up", "L Stick Down",
        "R Stick Left", "R Stick Right", "R Stick Up", "R Stick Down", "ZL", "ZL", "ZR", "ZR"
    };
    const int code = gamepad_[action];
    return code < axisBase ? names[code] : axes[code - axisBase];
}

bool Controls::capture(int action, bool controller)
{
    int code = key_;
    auto& profile = controller ? gamepad_ : keyboard_;
    if (controller) {
        code = -1;
        if (joystick_ < 0) return false;
        if (oldPad_.buttons[GLFW_GAMEPAD_BUTTON_B] && !gamepadBackHeld()) {
            code = GLFW_GAMEPAD_BUTTON_B;
        }
        for (int value = 0; value <= endAxis; ++value) {
            if (code >= 0) break;
            if (value == GLFW_GAMEPAD_BUTTON_B || (value > GLFW_GAMEPAD_BUTTON_LAST && value < axisBase) ||
                gamepadValue(value, pad_) < 0.72f || gamepadValue(value, oldPad_) > 0.56f) continue;
            code = value;
            break;
        }
    }
    if (code < 0) return false;
    const auto other = std::find(profile.begin(), profile.end(), code);
    if (other != profile.end()) std::swap(*other, profile[action]);
    profile[action] = code;
    save();
    return true;
}

void Controls::resetBindings(bool controller)
{
    if (controller) gamepad_ = defaultPad;
    else keyboard_ = defaultKeys;
    save();
}
