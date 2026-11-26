#include "renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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

bool pressed(GLFWwindow* window, int key)
{
    return glfwGetKey(window, key) == GLFW_PRESS;
}

DriveInput readInput(GLFWwindow* window)
{
    const bool accelerate = pressed(window, GLFW_KEY_W) || pressed(window, GLFW_KEY_UP);
    const bool reverse = pressed(window, GLFW_KEY_S) || pressed(window, GLFW_KEY_DOWN);
    const bool left = pressed(window, GLFW_KEY_A) || pressed(window, GLFW_KEY_LEFT);
    const bool right = pressed(window, GLFW_KEY_D) || pressed(window, GLFW_KEY_RIGHT);
    return {
        static_cast<float>(accelerate) - static_cast<float>(reverse),
        static_cast<float>(left) - static_cast<float>(right),
        pressed(window, GLFW_KEY_LEFT_SHIFT) || pressed(window, GLFW_KEY_RIGHT_SHIFT),
        pressed(window, GLFW_KEY_SPACE),
    };
}

struct CameraInput {
    bool looking = false;
    glm::dvec2 previousCursor{0.0};

    void update(GLFWwindow* window, ChaseCamera& camera, float dt)
    {
        const bool dragging = glfwGetWindowAttrib(window, GLFW_FOCUSED) &&
                              glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (dragging != looking) {
            glfwSetInputMode(window, GLFW_CURSOR, dragging ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            glfwGetCursorPos(window, &previousCursor.x, &previousCursor.y);
            looking = dragging;
        }
        glm::dvec2 cursor;
        glfwGetCursorPos(window, &cursor.x, &cursor.y);
        camera.update(glm::vec2(cursor - previousCursor), looking, dt);
        previousCursor = cursor;
    }
};

struct ActionKeys {
    bool down = false;
    bool up = false;
    bool mode = false;
    bool night = false;
    bool slimRider = false;
    bool gale = false;
};

void smokeTest(const CarAsset& asset, Renderer& renderer, TireEffects& effects, GLFWwindow* window)
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
    const float scale = std::min({height / 720.0f, width / 425.0f, 2.0f});
    std::array<unsigned char, 4> hudPixel{};
    glReadPixels(width - static_cast<int>(100.0f * scale), static_cast<int>(135.0f * scale),
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

void run(const std::filesystem::path& assets, CarModel model, bool smoke)
{
    CarAsset asset = loadCar(assets / carSpec(model).assetFile, model);
    const bool hasGale = std::filesystem::is_regular_file(assets / carSpec(CarModel::Gale).assetFile);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, smoke ? GLFW_FALSE : GLFW_TRUE);

    const std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(1280, 720, "RacingGame", nullptr, nullptr),
        glfwDestroyWindow);
    if (!window) {
        throw std::runtime_error("Could not create an OpenGL 3.3 window");
    }
    glfwMakeContextCurrent(window.get());
    if (!gladLoadGL(glfwGetProcAddress) || !GLAD_GL_VERSION_3_3) {
        throw std::runtime_error("Could not load OpenGL 3.3");
    }
    glfwSwapInterval(smoke ? 0 : 1);

    auto renderer = std::make_unique<Renderer>(asset);
    TireEffects effects;
    if (smoke) {
        if (model == CarModel::Gale) {
            smokeTestGale(asset, *renderer, effects, window.get());
        } else {
            smokeTest(asset, *renderer, effects, window.get());
        }
        return;
    }

    const auto updateTitle = [&] {
        std::string title(carSpec(asset.model).name);
        title += " | Right mouse look   Space handbrake   Shift brake   Q/E gears   M auto   N day/night";
        if (hasGale) {
            title += "   1 SlimRider   2 Gale";
        }
        glfwSetWindowTitle(window.get(), title.c_str());
    };
    updateTitle();

    CarState car;
    ChaseCamera camera;
    CameraInput cameraInput;
    ActionKeys previousKeys;
    bool night = false;
    double previousTime = glfwGetTime();
    double accumulator = 0.0;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        if (pressed(window.get(), GLFW_KEY_ESCAPE)) {
            break;
        }

        const double now = glfwGetTime();
        const double frameTime = std::min(now - previousTime, 0.1);
        accumulator += frameTime;
        previousTime = now;

        const ActionKeys keys{pressed(window.get(), GLFW_KEY_Q), pressed(window.get(), GLFW_KEY_E),
                              pressed(window.get(), GLFW_KEY_M), pressed(window.get(), GLFW_KEY_N),
                              pressed(window.get(), GLFW_KEY_1), pressed(window.get(), GLFW_KEY_2)};
        CarModel selected = asset.model;
        if (keys.slimRider && !previousKeys.slimRider) {
            selected = CarModel::SlimRider;
        } else if (hasGale && keys.gale && !previousKeys.gale) {
            selected = CarModel::Gale;
        }
        const bool switched = selected != asset.model;
        if (switched) {
            try {
                CarAsset nextAsset = loadCar(assets / carSpec(selected).assetFile, selected);
                renderer = std::make_unique<Renderer>(nextAsset);
                asset = std::move(nextAsset);
                updateTitle();
            } catch (const std::exception& error) {
                std::cerr << "Could not switch to " << carSpec(selected).name << ". " << error.what() << '\n';
            }
        }
        if ((switched && selected == asset.model) || pressed(window.get(), GLFW_KEY_R)) {
            car = {};
            camera = {};
            effects.clear();
            accumulator = 0.0;
        }
        if (keys.mode && !previousKeys.mode) {
            car.automatic = !car.automatic;
        }
        if (keys.night && !previousKeys.night) {
            night = !night;
        }
        const int shift = static_cast<int>(keys.up && !previousKeys.up) - static_cast<int>(keys.down && !previousKeys.down);
        if (shift != 0) {
            shiftGear(car, shift);
        }
        previousKeys = keys;
        cameraInput.update(window.get(), camera, static_cast<float>(frameTime));
        const DriveInput input = readInput(window.get());
        while (accumulator >= fixedStep) {
            drive(car, asset, input, fixedStep);
            effects.update(asset, car, fixedStep);
            accumulator -= fixedStep;
        }
        camera.updateSpeed(glm::length(car.velocity), static_cast<float>(frameTime));

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window.get(), &width, &height);
        if (width > 0 && height > 0) {
            renderer->render(asset, car, camera, effects, width, height, night);
            glfwSwapBuffers(window.get());
        } else {
            glfwWaitEventsTimeout(0.05);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    glfwSetErrorCallback([](int, const char* message) { std::cerr << message << '\n'; });
    if (!glfwInit()) {
        return 1;
    }

    int result = 0;
    try {
        const std::filesystem::path executable = std::filesystem::absolute(argv[0]);
        bool smoke = false;
        CarModel model = CarModel::SlimRider;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--smoke-test") {
                smoke = true;
            } else if (argument == "--gale") {
                model = CarModel::Gale;
            }
        }
        run(executable.parent_path() / "assets", model, smoke);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }

    glfwTerminate();
    return result;
}
