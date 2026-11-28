#pragma once

#include "driving.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <string>
#include <string_view>

enum class Action {
    Accelerate, Reverse, Left, Right, Brake, Handbrake, GearDown, GearUp, Automatic, Night, Reset, Pause,
    LookLeft, LookRight, LookUp, LookDown, Count
};

inline constexpr int actionCount = static_cast<int>(Action::Count);
inline constexpr std::array<std::string_view, actionCount> actionLabels{
    "Accelerate", "Reverse", "Steer left", "Steer right", "Brake", "Handbrake", "Shift down", "Shift up",
    "Toggle automatic", "Day / night", "Reset car", "Pause", "Look left", "Look right", "Look up", "Look down"
};

#ifndef __EMSCRIPTEN__
std::filesystem::path preferencesPath(const char* filename, const char* fallback);
#endif

struct MenuInput {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, back = false, reset = false, click = false, mouseMoved = false;
    glm::vec2 cursor{0.0f};
    int horizontalHeld = 0;
    bool mouseHeld = false;
};

class Controls {
public:
    explicit Controls(GLFWwindow* window);
    void poll();
    const MenuInput& menu() const { return menu_; }
    DriveInput drive() const;
    glm::vec2 look() const;
    bool justPressed(Action action) const;
    bool usingGamepad() const { return gamepadActive_; }
    bool gamepadBackHeld() const;
    bool mouseLook() const { return mouseLook_; }
    void setMouseLook(bool enabled);
    std::string binding(int action, bool controller) const;
    bool capture(int action, bool controller);
    void resetBindings(bool controller);

private:
    void load();
    void save() const;
    float gamepadValue(int code, const GLFWgamepadstate& state) const;

    GLFWwindow* window_;
    GLFWgamepadstate pad_{}, oldPad_{};
    int joystick_ = -1;
    int pendingKey_ = GLFW_KEY_UNKNOWN, key_ = GLFW_KEY_UNKNOWN;
    std::array<int, actionCount> keyboard_{}, gamepad_{};
    std::array<float, actionCount> values_{}, previousValues_{};
    MenuInput menu_, previousMenu_;
    bool gamepadActive_ = false, mouseLook_ = false;
};
