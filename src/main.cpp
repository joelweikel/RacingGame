#include "audio.hpp"
#include "hud.hpp"
#include "menu.hpp"

#ifdef __EMSCRIPTEN__
#include <GLFW/emscripten_glfw3.h>
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr float fixedStep = 1.0f / 120.0f;

struct CameraInput {
    bool looking = false;
    glm::dvec2 previousCursor{0.0};

    void update(GLFWwindow* window, ChaseCamera& camera, float dt, glm::vec2 look)
    {
        const bool dragging = glfwGetWindowAttrib(window, GLFW_FOCUSED) &&
                              glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (dragging != looking) {
#ifndef __EMSCRIPTEN__
            glfwSetInputMode(window, GLFW_CURSOR, dragging ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
#endif
            glfwGetCursorPos(window, &previousCursor.x, &previousCursor.y);
            looking = dragging;
        }
        glm::dvec2 cursor;
        glfwGetCursorPos(window, &cursor.x, &cursor.y);
        const auto delta = looking ? glm::vec2(cursor - previousCursor) : look * (390.0f * dt);
        camera.update(delta, looking || glm::length(look) > 0.0f, dt);
        previousCursor = cursor;
    }
};

void smokeTest(const CarAsset& asset, Renderer& renderer, TireEffects& effects, GLFWwindow* window,
               const std::filesystem::path& assets)
{
    CarState car;
    effects.update(asset, car, fixedStep);
    if (effects.markCount() != 0 || effects.smokeCount() != 0) {
        throw std::runtime_error("Stationary tires produced skid effects");
    }
    CarState launch;
    for (int step = 0; step < 120; ++step) {
        drive(launch, asset, {1.0f}, fixedStep);
    }
    if (launch.suspension.pitch < 0.008f || launch.wheels[2].load <= launch.wheels[0].load + 300.0f) {
        throw std::runtime_error("Suspension squat or rear weight transfer check failed");
    }
    for (int step = 0; step < 840; ++step) {
        drive(car, asset, {1.0f}, fixedStep);
    }
    if (car.position.x < 85.0f || car.velocity.x < 25.0f || car.gear < 2 ||
        car.wheels[0].angularSpeed <= 0.0f || std::abs(car.position.z) > 0.05f) {
        throw std::runtime_error("Straight-line acceleration, wheel rotation, or automatic shifting check failed");
    }
    const int automaticGear = car.gear;
    CarState manual = car;
    shiftGear(manual, -1);
    if (manual.automatic || manual.gear != automaticGear - 1 || manual.shiftTime <= 0.0f) {
        throw std::runtime_error("Manual downshift check failed");
    }
    shiftGear(manual, 1);
    if (manual.gear != automaticGear) {
        throw std::runtime_error("Manual upshift check failed");
    }
    CarState directionChange = car;
    for (int step = 0; step < 30; ++step) {
        drive(directionChange, asset, {-1.0f}, fixedStep);
    }
    if (!directionChange.brakeLights || directionChange.gear <= 0 || directionChange.velocity.x >= car.velocity.x - 1.0f) {
        throw std::runtime_error("Brake-before-reverse check failed");
    }
    if (directionChange.suspension.pitch >= -0.008f ||
        directionChange.wheels[0].load <= directionChange.wheels[2].load + 300.0f) {
        throw std::runtime_error("Suspension dive or front weight transfer check failed");
    }

    CarState turn;
    for (int step = 0; step < 300; ++step) {
        drive(turn, asset, {1.0f}, fixedStep);
    }
    CarState slide = turn;
    for (int step = 0; step < 60; ++step) {
        drive(turn, asset, {0.0f, 1.0f}, fixedStep);
        drive(slide, asset, {0.0f, 1.0f, false, true}, fixedStep);
        effects.update(asset, slide, fixedStep);
    }
    if (turn.position.z >= -0.3f || turn.heading <= 0.1f || turn.steering <= 0.1f) {
        throw std::runtime_error("Steering check failed");
    }
    const float outsideLoad = turn.wheels[0].load + turn.wheels[2].load;
    const float insideLoad = turn.wheels[1].load + turn.wheels[3].load;
    if (turn.suspension.roll <= 0.012f || outsideLoad <= insideLoad + 800.0f) {
        throw std::runtime_error("Body roll or lateral weight transfer check failed");
    }
    if (slide.wheels[2].angularSpeed != 0.0f || slide.wheels[3].angularSpeed != 0.0f ||
        slide.wheels[0].angularSpeed <= 1.0f || slide.heading <= turn.heading + 0.05f) {
        throw std::runtime_error("Rear handbrake lock or rotation check failed");
    }
    if (effects.markCount() < 8 || effects.smokeCount() < 4) {
        throw std::runtime_error("Sliding tires did not produce trails and smoke");
    }

    for (int step = 0; step < 840; ++step) {
        drive(car, asset, {0.0f, 0.0f, true}, fixedStep);
    }
    if (glm::length(car.velocity) > 0.07f) {
        throw std::runtime_error("Braking check failed");
    }
    if (std::abs(car.suspension.pitch) > 0.002f || std::abs(car.suspension.heave) > 0.002f) {
        throw std::runtime_error("Suspension did not settle after stopping");
    }

    const glm::vec3 stop = car.position;
    for (int step = 0; step < 120; ++step) {
        drive(car, asset, {-1.0f}, fixedStep);
    }
    if (car.brakeLights || car.gear >= 0 || glm::dot(car.position - stop, forward(car.heading)) >= -0.2f) {
        throw std::runtime_error("Reverse check failed");
    }

    ChaseCamera camera;
    camera.update({100.0f, -20.0f}, true, fixedStep);
    const float orbit = camera.yaw;
    camera.update({}, false, 0.4f);
    if (std::abs(orbit) < 0.5f || camera.yaw != orbit || camera.pitch >= 0.0f) {
        throw std::runtime_error("Camera look or recenter delay check failed");
    }
    camera.update({}, false, 2.0f);
    if (std::abs(camera.yaw) > 0.005f || std::abs(camera.pitch) > 0.005f) {
        throw std::runtime_error("Camera recenter check failed");
    }
    camera.updateSpeed(12.0f, fixedStep);
    if (camera.speedEffect != 0.0f) {
        throw std::runtime_error("Speed effects active at low speed");
    }
    camera.updateSpeed(68.0f, 1.0f);
    if (camera.speedEffect < 0.9f) {
        throw std::runtime_error("Speed effects did not ramp in");
    }
    camera.updateSpeed(3.0f, 2.0f);
    if (camera.speedEffect > 0.005f) {
        throw std::runtime_error("Speed effects did not ramp out");
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Cannot read the OpenGL framebuffer");
    }
    renderer.render(asset, car, ChaseCamera{}, effects, width, height);
    std::array<unsigned char, 4> groundPixel{};
    std::array<unsigned char, 4> skyPixel{};
    glReadPixels(12, 12, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, groundPixel.data());
    glReadPixels(12, height - 12, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, skyPixel.data());
    UiDraw ui(assets);
    ui.begin(width, height);
    drawHud(ui, car, carSpec(asset.model), courseName(Course::TestGrid), false);
    ui.finish();
    const auto accent = ui.toScreen({1100, 533});
    std::array<unsigned char, 4> hudPixel{};
    glReadPixels(static_cast<int>(accent.x), height - static_cast<int>(accent.y),
                 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hudPixel.data());
    if (glGetError() != GL_NO_ERROR || skyPixel[2] <= groundPixel[2] + 25 ||
        hudPixel[0] > 150 || hudPixel[1] < 200) {
        throw std::runtime_error("OpenGL scene or dashboard framebuffer check failed");
    }

    const auto capture = [&](const CarState& state, const ChaseCamera& view, bool night) {
        renderer.render(asset, state, view, effects, width, height, night);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    ChaseCamera effectView;
    effectView.yaw = -0.60f;
    effectView.pitch = 0.27f;
    const auto withSmoke = capture(slide, effectView, false);
    const std::size_t marks = effects.markCount();
    for (int step = 0; step < 260; ++step) {
        effects.update(asset, CarState{}, fixedStep);
    }
    if (effects.smokeCount() != 0 || effects.markCount() != marks) {
        throw std::runtime_error("Smoke or skid mark lifetime check failed");
    }
    const auto withMarks = capture(slide, effectView, false);
    effects.clear();
    const auto withoutEffects = capture(slide, effectView, false);
    if (glGetError() != GL_NO_ERROR || withSmoke == withMarks || withMarks == withoutEffects) {
        throw std::runtime_error("Skid trail or smoke framebuffer check failed");
    }

    const CarState parked;
    CarState braking;
    drive(braking, asset, {0.0f, 0.0f, true}, fixedStep);
    const auto day = capture(parked, ChaseCamera{}, false);
    ChaseCamera fastCamera;
    fastCamera.updateSpeed(70.0f, 1.0f);
    const auto fast = capture(parked, fastCamera, false);
    const auto skyCorner = (static_cast<std::size_t>(height - 8) * width + 8) * 4;
    if (fast[skyCorner + 2] >= day[skyCorner + 2] - 12) {
        throw std::runtime_error("High-speed vignette framebuffer check failed");
    }
    const auto dayBrake = capture(braking, ChaseCamera{}, false);
    const auto night = capture(parked, ChaseCamera{}, true);
    const auto nightBrake = capture(braking, ChaseCamera{}, true);
    const auto redLightPixels = [&](const auto& dark, const auto& illuminated) {
        int pixels = 0;
        for (int y = height / 7; y < height * 3 / 5; ++y) {
            for (int x = width / 3; x < width * 2 / 3; ++x) {
                const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
                const int red = illuminated[i];
                if (red >= dark[i] + 22 && red > 110 &&
                    red > illuminated[i + 1] * 1.5f && red > illuminated[i + 2] * 1.5f) {
                    ++pixels;
                }
            }
        }
        return pixels;
    };
    if (glGetError() != GL_NO_ERROR || redLightPixels(day, dayBrake) < 12 ||
        redLightPixels(day, night) < 12 || redLightPixels(night, nightBrake) < 12) {
        throw std::runtime_error("Rear running or brake lights did not illuminate in the correct modes");
    }

    renderer.render(asset, parked, ChaseCamera{}, effects, width / 2, height / 2);
    renderer.render(asset, parked, ChaseCamera{}, effects, width, height);
    const int scanWidth = width / 2;
    const int scanHeight = height / 2;
    const int cropWidth = scanWidth / 2;
    const int cropHeight = scanHeight / 2;
    std::vector<std::uint8_t> cameraCrop(static_cast<std::size_t>(cropWidth) * cropHeight * 4);
    for (const bool scanNight : {false, true}) {
        for (const float pitch : {-0.29f, 0.19f, 0.68f}) {
            ChaseCamera scan;
            scan.pitch = pitch;
            for (int yaw = 0; yaw < 12; ++yaw) {
                scan.yaw = yaw * (6.28318531f / 12.0f);
                renderer.render(asset, slide, scan, effects, scanWidth, scanHeight, scanNight);
                glReadPixels(scanWidth / 4, scanHeight / 4, cropWidth, cropHeight, GL_RGBA,
                             GL_UNSIGNED_BYTE, cameraCrop.data());
                int blackPixels = 0;
                for (std::size_t i = 0; i < cameraCrop.size(); i += 4) {
                    blackPixels += cameraCrop[i] <= 2 && cameraCrop[i + 1] <= 2 && cameraCrop[i + 2] <= 2;
                }
                if (blackPixels > 16) {
                    throw std::runtime_error("Unexpected black pixels near the car during camera orbit");
                }
            }
        }
    }
    if (glGetError() != GL_NO_ERROR) {
        throw std::runtime_error("Lighting framebuffer resize or camera orbit check failed");
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION) << '\n'
              << "Loaded " << asset.body.size() << " body primitives and " << asset.wheel.size()
              << " wheel primitives; verified camera orbit, suspension, speed effects, skid effects, and rear lights.\n";
}

void smokeTestGale(const CarAsset& asset, Renderer& renderer, TireEffects& effects, GLFWwindow* window)
{
    CarState car;
    drive(car, asset, {1.0f}, fixedStep);
    const float frontSpin = car.wheels[0].angularSpeed;
    const float rearSpin = car.wheels[2].angularSpeed;
    if (frontSpin <= 0.0f || std::abs(frontSpin / (frontSpin + rearSpin) - 0.30f) > 0.01f) {
        throw std::runtime_error("Gale AWD torque split check failed");
    }
    for (int step = 0; step < 1440; ++step) {
        drive(car, asset, {1.0f}, fixedStep);
    }
    if (car.gear < 2 || car.velocity.x < 30.0f || std::abs(car.position.z) > 0.05f) {
        throw std::runtime_error("Gale acceleration or automatic upshift check failed");
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    renderer.render(asset, car, ChaseCamera{}, effects, width, height);
    if (glGetError() != GL_NO_ERROR) {
        throw std::runtime_error("Gale framebuffer check failed");
    }
    std::cout << "Loaded Gale with " << asset.body.size() << " body primitives and "
              << asset.wheel.size() << " wheel primitives. AWD and automatic upshift verified.\n";
}

using Window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>;

class Game {
public:
    Game(Window gameWindow, const std::filesystem::path& assetDirectory, CarAsset initialAsset)
        : window(std::move(gameWindow)), assets(assetDirectory), controls(window.get()),
          menu(initialAsset.model, std::filesystem::is_regular_file(assets / carSpec(CarModel::Gale).assetFile)),
          ui(assets)
    {
        const auto i = static_cast<std::size_t>(initialAsset.model);
        cars[i] = std::move(initialAsset);
        renderers[i] = std::make_unique<Renderer>(cars[i]);
    }

    bool frame()
    {
        glfwPollEvents();
        if (glfwWindowShouldClose(window.get())) return false;

        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::clamp(now - previousTime, 0.0, 0.1));
        previousTime = now;

        int width = 0, height = 0, displayWidth = 0, displayHeight = 0;
        glfwGetFramebufferSize(window.get(), &width, &height);
        glfwGetWindowSize(window.get(), &displayWidth, &displayHeight);
        if (width <= 0 || height <= 0 || displayWidth <= 0 || displayHeight <= 0) {
#ifndef __EMSCRIPTEN__
            glfwWaitEventsTimeout(0.05);
#endif
            return true;
        }

        ui.begin(width, height);
        controls.poll();
        const auto cursor = controls.menu().cursor * glm::vec2(
            static_cast<float>(width) / displayWidth, static_cast<float>(height) / displayHeight);
        menu.update(controls, ui.fromScreen(cursor), dt);
        if (menu.screen() != lastScreen) {
            lastScreen = menu.screen();
            updateTitle();
        }
        const bool driving = menu.screen() == Screen::Drive;
        controls.setMouseLook(driving);
        if (menu.takeRaceStart()) {
            reset();
            night = false;
        }

        const SceneView view = menu.scene(ui, width, height);
        const auto model = menu.displayedCar();
        const std::size_t i = static_cast<std::size_t>(model);
        const CarAsset& asset = sceneAsset(model, i);
        const DriveInput input = driving ? controls.drive() : DriveInput{};
        if (driving) {
            if (controls.justPressed(Action::Reset)) reset();
            if (controls.justPressed(Action::Automatic)) car.automatic = !car.automatic;
            if (controls.justPressed(Action::Night)) night = !night;
            const int shift = static_cast<int>(controls.justPressed(Action::GearUp)) -
                              static_cast<int>(controls.justPressed(Action::GearDown));
            if (shift != 0) shiftGear(car, shift);
            const auto look = controls.look();
            cameraInput.update(window.get(), camera, dt, look);
            accumulator += dt;
            while (accumulator >= fixedStep) {
                drive(car, asset, input, fixedStep);
                effects.update(asset, car, fixedStep);
                accumulator -= fixedStep;
            }
            camera.updateSpeed(glm::length(car.velocity), dt);
        }
        audio.update(model, car, input, driving);

        ChaseCamera orbit;
        orbit.yaw = 2.34f + 0.34f * std::sin(menu.time() * 0.66f);
        orbit.pitch = -0.065f + 0.04f * std::sin(menu.time() * 0.81f);
        renderers[i]->render(asset, view.menu ? CarState{} : car, view.menu ? orbit : camera,
                             effects, width, height, !view.menu && night, view);
        if (!view.menu) {
            drawHud(ui, car, carSpec(menu.playerCar()), courseName(menu.course()), controls.usingGamepad());
        }
        menu.draw(ui, controls);
        ui.finish();
#ifndef __EMSCRIPTEN__
        glfwSwapBuffers(window.get());
#endif
        return true;
    }

private:
    void updateTitle() const
    {
        const std::array<const char*, 7> sections{"", "Main Menu", "Car Select", "Map Select", "Controls", "", "Paused"};
        std::string title = sections[static_cast<std::size_t>(lastScreen)];
        if (lastScreen == Screen::Drive) {
            title = std::string(carSpec(menu.playerCar()).name) + " | " + std::string(courseName(menu.course()));
        }
        if (!title.empty()) title += " | ";
        title += "Racing Game";
        glfwSetWindowTitle(window.get(), title.c_str());
    }

    void reset()
    {
        car = {};
        camera = {};
        effects.clear();
        accumulator = 0.0;
    }

    const CarAsset& sceneAsset(CarModel model, std::size_t i)
    {
        if (!renderers[i]) {
            cars[i] = loadCar(assets / carSpec(model).assetFile, model);
            renderers[i] = std::make_unique<Renderer>(cars[i]);
        }
        return cars[i];
    }

    Window window;
    std::filesystem::path assets;
    Controls controls;
    Menu menu;
    UiDraw ui;
    std::array<CarAsset, 2> cars;
    std::array<std::unique_ptr<Renderer>, 2> renderers;
    TireEffects effects;
    GameAudio audio;
    CarState car;
    ChaseCamera camera;
    CameraInput cameraInput;
    Screen lastScreen = Screen::Splash;
    bool night = false;
    double previousTime = glfwGetTime();
    double accumulator = 0.0;
};

void run(const std::filesystem::path& assets, CarModel model, bool smoke)
{
    CarAsset asset = loadCar(assets / carSpec(model).assetFile, model);
#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2); // The browser port takes the WebGL version.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, smoke ? GLFW_FALSE : GLFW_TRUE);

    Window window(glfwCreateWindow(1280, 720, "Racing Game", nullptr, nullptr), glfwDestroyWindow);
    if (!window) {
        throw std::runtime_error("Could not create the graphics window");
    }
    glfwMakeContextCurrent(window.get());
#ifndef __EMSCRIPTEN__
    if (!gladLoadGL(glfwGetProcAddress) || !GLAD_GL_VERSION_3_3) {
        throw std::runtime_error("Could not load OpenGL 3.3");
    }
    glfwSwapInterval(smoke ? 0 : 1);
#endif

    if (smoke) {
        Renderer renderer(asset);
        TireEffects effects;
        if (model == CarModel::Gale) {
            smokeTestGale(asset, renderer, effects, window.get());
        } else {
            smokeTest(asset, renderer, effects, window.get(), assets);
        }
        return;
    }

#ifdef __EMSCRIPTEN__
    emscripten::glfw3::MakeCanvasResizable(window.get(), "#viewport");
    glfwSetMouseButtonCallback(window.get(), [](GLFWwindow* handle, int button, int action, int) {
        const auto* controls = static_cast<const Controls*>(glfwGetWindowUserPointer(handle));
        if (controls && controls->mouseLook() && button == GLFW_MOUSE_BUTTON_RIGHT) {
            glfwSetInputMode(handle, GLFW_CURSOR, action == GLFW_PRESS ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
    });
    auto game = std::make_unique<Game>(std::move(window), assets, std::move(asset));
    emscripten_set_main_loop_arg([](void* argument) {
        auto* activeGame = static_cast<Game*>(argument);
        try {
            if (activeGame->frame()) {
                return;
            }
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
        }
        emscripten_cancel_main_loop();
        delete activeGame;
        glfwTerminate();
    }, game.release(), 0, false);
#else
    Game game(std::move(window), assets, std::move(asset));
    while (game.frame()) {
    }
#endif
}

} // namespace

int main(int argc, char** argv)
{
    glfwSetErrorCallback([](int, const char* message) { std::cerr << message << '\n'; });
    if (!glfwInit()) {
        return 1;
    }

    int result = 0;
    bool smoke = false;
    try {
        CarModel model = CarModel::SlimRider;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--smoke-test") {
                smoke = true;
            } else if (argument == "--gale") {
                model = CarModel::Gale;
            }
        }
#ifdef __EMSCRIPTEN__
        run("/assets", model, smoke);
#else
        const std::filesystem::path executable = std::filesystem::absolute(argv[0]);
        run(executable.parent_path() / "assets", model, smoke);
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }

#ifdef __EMSCRIPTEN__
    if (smoke || result != 0) {
        glfwTerminate();
    }
#else
    glfwTerminate();
#endif
    return result;
}
