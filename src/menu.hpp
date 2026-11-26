#pragma once

#include "controls.hpp"
#include "renderer.hpp"
#include "ui_draw.hpp"

enum class Screen { Splash, Main, Cars, Maps, Settings, Drive, Pause };

std::string_view courseName(Course course);

class Menu {
public:
    explicit Menu(CarModel initialModel, bool hasGale);
    void update(Controls& controls, glm::vec2 cursor, float dt);
    void draw(UiDraw& ui, const Controls& controls) const;
    SceneView scene(const UiDraw& ui, int width, int height) const;
    CarModel displayedCar() const;
    CarModel playerCar() const { return selectedCar_; }
    Course course() const { return course_; }
    Screen screen() const { return screen_; }
    bool takeRaceStart();
    float time() const { return time_; }

private:
    void open(Screen screen);
    void transition(Screen screen, glm::vec2 origin);
    UiBox option(int index) const;
    int count() const;
    int firstBinding() const;

    Screen screen_ = Screen::Splash, pending_ = Screen::Splash, settingsReturn_ = Screen::Main;
    CarModel selectedCar_;
    Course course_ = Course::TestGrid;
    bool hasGale_, controllerBindings_ = true, startingRace_ = false;
    int focus_ = 0, capture_ = -1;
    float time_ = 0.0f, entered_ = 0.0f, rippleAt_ = -10.0f, cancelHeld_ = 0.0f;
    glm::vec2 rippleOrigin_{0.0f};
};
