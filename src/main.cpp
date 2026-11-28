#include "audio.hpp"
#include "camp.hpp"
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

struct TitleDrift {
    CarState car;
    TireEffects effects{400, 1.85f};

    void update(const CarAsset& asset, float dt)
    {
        if (!primed_) {
            // Fill the smoke trail and lay down tread before the first visible frame.
            for (int i = 0; i < 270; ++i) advance(asset, fixedStep);
            primed_ = true;
        }
        advance(asset, dt * 0.16f);
    }

    ChaseCamera camera() const
    {
        ChaseCamera view;
        view.yaw = 2.16f + 0.24f * std::sin(phase_);
        view.pitch = -0.017f + 0.035f * std::sin(phase_ * 2.0f);
        return view;
    }

private:
    void advance(const CarAsset& asset, float dt)
    {
        constexpr float radius = 14.0f;
        constexpr float turnRate = 0.68f;
        constexpr float tau = 6.283185307f;
        phase_ = std::remainder(phase_ + turnRate * dt, tau);
        car.position = radius * glm::vec3(std::sin(phase_), 0.0f, -std::cos(phase_));
        car.velocity = forward(-phase_) * (radius * turnRate);
        car.heading = -phase_ - 0.60f - 0.11f * std::sin(phase_);
        car.yawRate = -turnRate * (1.0f + 0.11f * std::cos(phase_));
        car.steering = 0.38f + 0.055f * std::sin(phase_ * 2.0f);
        car.suspension.roll = -0.041f - 0.012f * std::sin(phase_ * 2.0f);
        car.suspension.pitch = 0.014f * std::cos(phase_);
        for (std::size_t i = 0; i < car.wheels.size(); ++i) {
            auto& wheel = car.wheels[i];
            const bool rear = i >= 2;
            wheel.slip = rear ? 0.91f + 0.08f * std::sin(phase_ + static_cast<float>(i)) : 0.11f;
            const float spin = (rear ? 1.42f : 1.0f) * radius * turnRate / asset.wheelRadius;
            wheel.angle = std::remainder(wheel.angle + spin * dt, tau);
        }
        effects.update(asset, car, dt);
    }

    float phase_ = -1.52f;
    bool primed_ = false;
};

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

void smokeTestHandling(const CarAsset& asset)
{
    CarState launch;
    for (int step = 0; step < 120; ++step) {
        drive(launch, asset, {1.0f}, fixedStep);
        const float slipReference = std::max(2.5f, launch.velocity.x);
        for (const auto& wheel : launch.wheels) {
            if (wheel.angularSpeed * asset.wheelRadius - launch.velocity.x > 0.16f * slipReference) {
                throw std::runtime_error("Traction control allowed excessive wheelspin during launch");
            }
        }
    }

    const auto rolling = [&](float speed) {
        CarState car;
        car.velocity.x = speed;
        for (auto& wheel : car.wheels) {
            wheel.angularSpeed = speed / asset.wheelRadius;
        }
        return car;
    };
    CarState corner = rolling(20.0f);
    for (int step = 0; step < 120; ++step) {
        drive(corner, asset, {0.0f, 1.0f}, fixedStep);
    }
    const glm::vec3 cornerDirection = forward(corner.heading);
    const float cornerSpeed = glm::length(corner.velocity);
    if (cornerSpeed < 17.0f || glm::dot(corner.velocity, cornerDirection) < 0.99f * cornerSpeed ||
        corner.position.z > -2.0f) {
        throw std::runtime_error("Coasting turn lost grip or failed to rotate");
    }

    CarState powerTurn = rolling(10.0f);
    for (int step = 0; step < 60; ++step) {
        drive(powerTurn, asset, {1.0f, 1.0f}, fixedStep);
    }
    const float frontSlip = std::max(powerTurn.wheels[0].slip, powerTurn.wheels[1].slip);
    const float rearSlip = std::max(powerTurn.wheels[2].slip, powerTurn.wheels[3].slip);
    if (frontSlip > 0.45f || powerTurn.heading < 0.2f ||
        (asset.model == CarModel::SlimRider ? rearSlip < 0.65f : rearSlip > 0.45f)) {
        throw std::runtime_error("Power turn lost front grip or failed to preserve the car's handling balance");
    }
}

void smokeTestDonuts(const CarAsset& asset)
{
    for (const float steering : {-1.0f, 1.0f}) {
        CarState car;
        for (int step = 0; step < 720; ++step) {
            drive(car, asset, {1.0f, steering}, fixedStep);
        }

        const glm::vec3 start = car.position;
        float rotation = 0.0f;
        for (int step = 0; step < 600; ++step) {
            drive(car, asset, {1.0f, steering}, fixedStep);
            const glm::vec3 side = glm::cross(forward(car.heading), glm::vec3{0.0f, 1.0f, 0.0f});
            const float speed = glm::length(car.velocity);
            if (speed < 3.0f || speed > 8.0f || glm::dot(car.velocity, side) * steering < 0.22f * speed ||
                std::max(car.wheels[2].slip, car.wheels[3].slip) < 0.65f || car.brakeLights ||
                glm::length(car.position - start) > 10.0f) {
                throw std::runtime_error("SlimRider failed to sustain a tight donut");
            }
            rotation += car.yawRate * steering * fixedStep;
        }
        if (rotation < 6.28318531f) {
            throw std::runtime_error("SlimRider did not complete a full donut rotation");
        }

        for (int step = 0; step < 180; ++step) {
            drive(car, asset, {0.45f}, fixedStep);
        }
        if (std::abs(car.yawRate) > 0.1f || std::max(car.wheels[2].slip, car.wheels[3].slip) > 0.2f) {
            throw std::runtime_error("SlimRider did not regain traction after exiting the donut");
        }
    }
}

void smokeTestOvalSurfaces(const CarAsset& asset, TireEffects& effects)
{
    constexpr Course course = Course::DustlineOval;
    CarState road, sand;
    sand.position.z = 18.0f;
    road.velocity.x = sand.velocity.x = 22.0f;
    for (CarState* state : {&road, &sand}) {
        for (auto& wheel : state->wheels) wheel.angularSpeed = state->velocity.x / asset.wheelRadius;
        for (int step = 0; step < 60; ++step) drive(*state, asset, {0.0f, 0.0f, true}, fixedStep, nullptr, course);
    }
    if (road.velocity.x >= sand.velocity.x - 1.5f) {
        throw std::runtime_error("Dustline Oval asphalt did not brake harder than sand");
    }

    CarState sliding;
    sliding.velocity.x = 22.0f;
    for (auto& wheel : sliding.wheels) wheel.slip = 0.8f;
    const auto advance = [&](float z, int steps) {
        sliding.position.z = z;
        for (int step = 0; step < steps; ++step) {
            sliding.position += sliding.velocity * fixedStep;
            effects.update(asset, sliding, fixedStep, nullptr, course);
        }
    };
    const auto mixedZ = ovalRoadHalfWidth;
    advance(mixedZ, 15);
    if (effects.markCount() == 0 || effects.markCount() > 28) {
        throw std::runtime_error("Dustline Oval did not restrict marks to the paved-side tires");
    }
    const auto pavedMarks = effects.markCount();
    for (auto& wheel : sliding.wheels) wheel.slip = 0.0f;
    advance(18.0f, 20);
    if (effects.markCount() != pavedMarks) throw std::runtime_error("Dustline Oval sand added rubber");
    effects.clear();
    advance(18.0f, 20);
    if (effects.markCount() != 0 || effects.smokeCount() == 0) {
        throw std::runtime_error("Rolling on Dustline Oval sand did not kick up dust");
    }
    effects.clear();
}

void smokeTest(const CarAsset& asset, Renderer& renderer, TireEffects& effects, GLFWwindow* window,
               const std::filesystem::path& assets)
{
    smokeTestDonuts(asset);
    smokeTestOvalSurfaces(asset, effects);
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
    drawHud(ui, car, asset.model, courseName(Course::TestGrid), "Esc", true);
    ui.finish();
    const auto view = ui.viewportBounds();
    const auto accent = ui.toScreen({view.x + view.w - 264, view.y + view.h - 121});
    std::array<unsigned char, 4> hudPixel{};
    glReadPixels(static_cast<int>(accent.x), height - static_cast<int>(accent.y),
                 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hudPixel.data());
    if (glGetError() != GL_NO_ERROR || skyPixel[2] <= groundPixel[2] + 25 ||
        hudPixel[0] < 200 || hudPixel[1] > 120) {
        throw std::runtime_error("OpenGL scene or dashboard framebuffer check failed");
    }

    const auto readFrame = [&]() {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    const auto capture = [&](const CarState& state, const ChaseCamera& view, bool night) {
        renderer.render(asset, state, view, effects, width, height, night);
        return readFrame();
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

    CarAsset unlit = asset;
    unlit.headlights.fill(glm::vec3(1000.0f));
    renderer.render(unlit, parked, ChaseCamera{}, effects, width, height, true);
    const auto noHeadlightSpill = readFrame();
    int illuminatedRoad = 0, brakeSpill = 0;
    for (int x = width * 2 / 5; x < width * 3 / 5; ++x) {
        for (int y = height * 3 / 5; y < height * 3 / 4; ++y) {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            illuminatedRoad += night[i] >= noHeadlightSpill[i] + 14;
        }
        for (int y = height / 14; y < height / 5; ++y) {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            brakeSpill += nightBrake[i] >= night[i] + 14;
        }
    }
    if (illuminatedRoad < 70 || brakeSpill < 70) {
        throw std::runtime_error("Headlights or brake lights failed to illuminate the road");
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

void smokeTestGarage(const CarAsset& asset, TireEffects& effects)
{
    if (asset.gasolineExhaust.empty()) return; // Legacy exports have no exhaust markers.
    const bool gale = asset.model == CarModel::Gale;
    if (asset.gasolineExhaust.size() != (gale ? 2u : 1u) ||
        asset.dieselExhaust.size() != (gale ? 2u : 1u) || (gale && asset.wheel.size() < 6)) {
        throw std::runtime_error("Missing authored exhaust sockets or Gale wheel components");
    }
    for (const auto option : {CarPart::HoodIntake, CarPart::Splitter, CarPart::Wing, CarPart::Diesel}) {
        if (std::none_of(asset.body.begin(), asset.body.end(), [&](const MeshPart& part) {
            return part.carPart == option;
        })) throw std::runtime_error("Missing authored garage option");
    }

    CarState stock, intake;
    intake.setup.hoodIntake = true;
    stock.velocity.x = intake.velocity.x = 20.0f;
    for (CarState* variant : {&stock, &intake}) {
        for (auto& wheel : variant->wheels) wheel.angularSpeed = variant->velocity.x / asset.wheelRadius;
        for (int step = 0; step < 120; ++step) drive(*variant, asset, {0.55f}, fixedStep);
    }
    if (intake.velocity.x <= stock.velocity.x) throw std::runtime_error("Hood intake did not increase acceleration");

    CarState aero = stock;
    aero.setup.splitter = aero.setup.wing = true;
    for (int step = 0; step < 14400; ++step) {
        drive(stock, asset, {1.0f}, fixedStep);
        drive(aero, asset, {1.0f}, fixedStep);
    }
    if (aero.velocity.x >= stock.velocity.x - 1.0f || aero.suspension.heave >= stock.suspension.heave - 0.008f) {
        throw std::runtime_error("Aero kit did not reduce top speed and load the suspension");
    }

    CarState diesel;
    diesel.setup.diesel = true;
    const DriveInput rev{1.0f, 0.0f, true};
    for (int step = 0; step < 20; ++step) {
        drive(diesel, asset, rev, fixedStep);
        effects.update(asset, diesel, fixedStep);
    }
    const float initialBoost = diesel.boost;
    if (diesel.soot <= 0.2f || effects.smokeCount() == 0 || initialBoost >= 0.4f) {
        throw std::runtime_error("Diesel failed to roll coal before the turbo spooled");
    }
    for (int step = 0; step < 220; ++step) drive(diesel, asset, rev, fixedStep);
    if (diesel.boost <= initialBoost + 0.35f) throw std::runtime_error("Diesel turbo failed to build boost under load");
    const float speed = diesel.turboSpeed;
    drive(diesel, asset, {}, fixedStep);
    if (diesel.turboSpeed >= speed || diesel.turboSpeed <= speed * 0.8f) {
        throw std::runtime_error("Diesel turbine failed to coast down");
    }

    effects.clear();
    CarState gasoline;
    gasoline.rpm = 4300.0f;
    shiftGear(gasoline, 1);
    for (int step = 0; step < 8; ++step) {
        drive(gasoline, asset, {1.0f}, fixedStep);
        effects.update(asset, gasoline, fixedStep);
    }
    if (effects.smokeCount() == 0) throw std::runtime_error("Gasoline shift failed to emit exhaust flame");
    effects.clear();
}

void smokeTestGale(const CarAsset& asset, Renderer& renderer, TireEffects& effects, GLFWwindow* window)
{
    // Equal axle loads isolate the torque split from the weight-dependent tire response.
    CarAsset centered;
    centered.model = asset.model;
    centered.wheelRadius = asset.wheelRadius;
    centered.hubs = asset.hubs;
    const float halfWheelbase = (asset.hubs[0].x - asset.hubs[2].x) * 0.5f;
    for (auto& hub : centered.hubs) {
        hub.x = std::copysign(halfWheelbase, hub.x);
    }
    CarState torqueCheck;
    drive(torqueCheck, centered, {1.0f}, fixedStep);
    const float frontSpin = torqueCheck.wheels[0].angularSpeed;
    const float rearSpin = torqueCheck.wheels[2].angularSpeed;
    if (frontSpin <= 0.0f || std::abs(frontSpin / (frontSpin + rearSpin) - 0.30f) > 0.01f) {
        throw std::runtime_error("Gale AWD torque split check failed");
    }
    CarState car;
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

void smokeTestCamp(const CarAsset& asset, Renderer& renderer, TireEffects& effects, GLFWwindow* window)
{
    CampWorld camp;
    {
        CampWorld grass;
        const CampTile grassTile{0, 1};
        const auto plants = grass.buildGrass(grassTile, 23, GrassBand::Near);
        const auto repeat = grass.buildGrass(grassTile, 23, GrassBand::Near);
        const auto middle = grass.buildGrass(grassTile, 23, GrassBand::Middle);
        const auto farPlants = grass.buildGrass(grassTile, 23, GrassBand::Far);
        if (farPlants.empty() || farPlants.size() >= middle.size() || middle.size() >= plants.size() ||
            repeat.size() != plants.size() || repeat.front().root != plants.front().root) {
            throw std::runtime_error("Camp grass did not regenerate from the world seed");
        }
        const auto distant = grass.buildGrass(grassTile, 23, GrassBand::Far, true);
        const auto horizon = grass.buildGrass(grassTile, 23, GrassBand::Horizon, true);
        if (distant.size() + horizon.size() != farPlants.size()) {
            throw std::runtime_error("The horizon grass band duplicated or dropped plants");
        }
        const glm::vec3 plant = plants.front().root + glm::vec3(0.0f, 0.0f, campGrassTileSize);
        const glm::dvec2 point{plant.x, plant.z};
        if (grass.surface(plant).surface != GroundSurface::Dirt ||
            grass.surface(grass.spawn()).surface != GroundSurface::Asphalt) {
            throw std::runtime_error("Camp tire surfaces did not follow the road and grass");
        }
        CarState trampler;
        const auto& tire = asset.hubs[0];
        trampler.position = plant - glm::vec3(tire.x + 0.45f, 0.0f, tire.z);
        grass.trampleGrass(asset, trampler, fixedStep);
        trampler.position.x += 0.9f;
        grass.trampleGrass(asset, trampler, fixedStep);
        const auto pressed = grass.grassTrack(point);
        if (pressed.flattened < 0.85f || pressed.direction.x < 0.95f) {
            throw std::runtime_error("A swept tire failed to flatten grass in its direction of travel");
        }
        trampler.position.x += 4.0f;
        grass.trampleGrass(asset, trampler, fixedStep);
        glm::vec3 far{1025.0f, 0.0f, 0.0f};
        trampler.position -= grass.rebase(far);
        grass.trampleGrass(asset, trampler, 5.0f);
        const float recovering = grass.grassTrack(point).flattened;
        grass.trampleGrass(asset, trampler, 5.0f);
        if (recovering < 0.1f || recovering >= pressed.flattened || grass.grassTrack(point).flattened != 0.0f) {
            throw std::runtime_error("Camp grass failed to recover across a world origin shift");
        }
    }
    const auto first = camp.buildChunk({0, 0});
    const auto next = camp.buildChunk({1, 0});
    const auto position = [&](const CampChunk& chunk, const Vertex& vertex) {
        return camp.localOrigin(chunk.origin) + vertex.position;
    };
    if (first.surfaces[1].indices.empty() || first.surfaces[2].indices.empty()) {
        throw std::runtime_error("Camp road or gravel pullout was not generated");
    }
    const auto& firstTerrain = first.surfaces[0].vertices;
    const auto& nextTerrain = next.surfaces[0].vertices;
    const std::size_t row = firstTerrain.size() / 33;
    for (std::size_t i = 0; i < row; ++i) {
        if (glm::length(position(first, firstTerrain[32 * row + i]) - position(next, nextTerrain[i])) > 0.001f) {
            throw std::runtime_error("Camp terrain seam did not match");
        }
    }
    const auto detailed = camp.buildChunk({0, 1});
    const auto distant = camp.buildChunk({1, 1}, 4);
    const auto& distantMesh = distant.surfaces[0];
    if (distantMesh.indices.size() >= detailed.surfaces[0].indices.size() / 4) {
        throw std::runtime_error("Distant terrain did not reduce mesh detail");
    }
    for (const auto& vertex : detailed.surfaces[0].vertices) {
        if (vertex.position.x != static_cast<float>(campTerrainTileSize)) continue;
        const auto seam = position(detailed, vertex);
        if (std::none_of(distantMesh.vertices.begin(), distantMesh.vertices.end(), [&](const Vertex& other) {
            return glm::length(seam - position(distant, other)) < 0.001f;
        })) throw std::runtime_error("Distant terrain did not match the full-detail seam");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& mesh = first.surfaces[i];
        for (std::size_t j = 0; j + 2 < mesh.indices.size(); j += 21) {
            const glm::vec3 point = (position(first, mesh.vertices[mesh.indices[j]]) +
                position(first, mesh.vertices[mesh.indices[j + 1]]) + position(first, mesh.vertices[mesh.indices[j + 2]])) / 3.0f;
            if (std::abs(camp.surface(point).height - point.y) > 0.001f) {
                throw std::runtime_error("Camp wheel contact did not match the rendered terrain");
            }
        }
    }
    const auto shelter = std::find_if(first.props.begin(), first.props.end(), [](const auto& prop) {
        return prop.kind == CampProp::Shelter;
    });
    if (shelter == first.props.end()) throw std::runtime_error("Camp shelter was not generated");
    glm::vec3 obstacle = camp.localOrigin(first.origin) + shelter->center;
    glm::vec3 impact = obstacle + glm::vec3(shelter->size.x * 0.5f + 0.2f, 0.0f, 0.0f);
    glm::vec3 velocity{-10.0f, 0.0f, 0.0f};
    camp.collide(impact, velocity);
    if (impact.x < obstacle.x + shelter->size.x * 0.5f + 0.8f || velocity.x < 0.0f) {
        throw std::runtime_error("Camp shelter collision did not stop the car");
    }

    CarState car;
    car.position = camp.spawn();
    car.heading = camp.spawnHeading();
    placeOnCampSurface(car, asset, camp);
    for (int step = 0; step < 240; ++step) drive(car, asset, {1.0f}, fixedStep, &camp);
    if (car.position.x < 4.0f || !std::isfinite(car.groundSlope.x) ||
        std::abs(car.position.y - camp.surface(car.position).height) > 0.6f) {
        throw std::runtime_error("Camp driving did not follow the terrain");
    }

    CarState banked;
    bool foundBank = false, foundUneven = false;
    for (int x = 15; x < 105; x += 7) {
        for (int z = -42; z < 43; z += 5) {
            CarState slope;
            slope.position = {static_cast<float>(x), 0.0f, static_cast<float>(z)};
            slope.heading = 0.7f;
            placeOnCampSurface(slope, asset, camp);
            if (std::abs(slope.groundSlope.x) < 0.12f || std::abs(slope.groundSlope.y) < 0.22f) continue;
            const auto frame = campGroundFrame(slope);
            for (std::size_t i = 0; i < slope.wheels.size(); ++i) {
                const auto& hub = asset.hubs[i];
                const float travel = slope.wheels[i].groundTravel;
                if (std::abs(travel) >= asset.wheelRadius - 0.001f) continue;
                const auto contact = slope.position + frame * glm::vec3(hub.x, travel, hub.z);
                if (std::abs(contact.y - camp.surface(contact).height) > 0.016f) {
                    throw std::runtime_error("Camp wheel lost contact on a banked slope");
                }
                foundUneven |= std::abs(travel) > 0.035f;
            }
            if (!foundBank) banked = slope;
            foundBank = true;
        }
    }
    if (!foundBank || !foundUneven) throw std::runtime_error("Camp slope check did not exercise suspension travel");

    SceneView scene;
    scene.course = Course::DerelictCamp;
    scene.camp = &camp;
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    for (int frame = 0; frame < 8; ++frame) {
        renderer.render(asset, car, ChaseCamera{}, effects, width / 2, height / 2, true, scene);
    }
    std::array<unsigned char, 4> night{}, day{};
    glReadPixels(width / 8, height / 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, night.data());
    renderer.render(asset, car, ChaseCamera{}, effects, width, height, false, scene);
    glReadPixels(width / 4, height / 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, day.data());
    if (night == day || night[2] < 30 || glGetError() != GL_NO_ERROR) {
        throw std::runtime_error("Camp lighting or fog framebuffer check failed");
    }

    banked.steering = 0.35f;
    renderer.render(asset, banked, ChaseCamera{}, effects, width / 2, height / 2, false, scene);
    car.position = {10993.25f, 0.0f, -1322.25f};
    const auto tile = camp.tileAt(car.position);
    const float before = camp.surface(car.position).height;
    camp.rebase(car.position);
    if (tile != camp.tileAt(car.position) || std::abs(camp.surface(car.position).height - before) > 0.001f) {
        throw std::runtime_error("Camp streaming origin changed the terrain");
    }
    placeOnCampSurface(car, asset, camp);
    renderer.render(asset, car, ChaseCamera{}, effects, width / 2, height / 2, true, scene);
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Camp streaming render failed after a long-distance drive");
    std::cout << "Camp terrain, wheel contact, collisions, fog, and streaming verified.\n";
}

using Window = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>;

class Game {
public:
    Game(Window gameWindow, const std::filesystem::path& assetDirectory, CarAsset initialAsset)
        : window(std::move(gameWindow)), assets(assetDirectory), controls(window.get()),
          menu(initialAsset.model, std::filesystem::is_regular_file(assets / carSpec(CarModel::Gale).assetFile), assets),
          ui(assets), audio(assets)
    {
        const auto i = static_cast<std::size_t>(initialAsset.model);
        cars[i] = std::move(initialAsset);
        renderers[i] = std::make_unique<Renderer>(cars[i], assets);
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
        const bool startingFromMap = lastScreen == Screen::Maps;
        const bool enteringGarage = menu.screen() == Screen::Garage && lastScreen != Screen::Garage;
        if (menu.screen() != lastScreen) {
            lastScreen = menu.screen();
            updateTitle();
        }
        const bool driving = menu.screen() == Screen::Drive;
        controls.setMouseLook(driving);
        if (menu.takeRaceStart()) {
            if (startingFromMap && menu.course() == Course::DerelictCamp) {
                camp = CampWorld(static_cast<std::uint32_t>(glfwGetTimerValue()));
            }
            reset();
            night = menu.course() == Course::DerelictCamp;
        }

        SceneView view = menu.scene(ui, width, height);
        view.camp = !view.menu && view.course == Course::DerelictCamp ? &camp : nullptr;
        const auto model = menu.displayedCar();
        const std::size_t i = static_cast<std::size_t>(model);
        const CarAsset& asset = sceneAsset(model, i);
        renderers[i]->setGraphics(menu.graphics());
        if (enteringGarage) menu.setGarageAsset(asset);
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
                drive(car, asset, input, fixedStep, view.camp, view.course);
                effects.update(asset, car, fixedStep, view.camp, view.course);
                if (view.camp) camp.trampleGrass(asset, car, fixedStep);
                accumulator -= fixedStep;
            }
            if (view.camp && (std::abs(car.position.x) >= 1024.0f || std::abs(car.position.z) >= 1024.0f)) {
                effects.shiftOrigin(camp.rebase(car.position));
            }
            camera.updateSpeed(glm::length(car.velocity), dt);
        }
        audio.update(model, car, driving);

        ChaseCamera orbit;
        orbit.yaw = 2.34f + 0.34f * std::sin(menu.time() * 0.66f);
        orbit.pitch = -0.065f + 0.04f * std::sin(menu.time() * 0.81f);
        if (menu.screen() == Screen::Garage) orbit = menu.garageCamera();
        if (view.titleDrift) {
            titleDrift.update(asset, dt);
            renderers[i]->render(asset, titleDrift.car, titleDrift.camera(), titleDrift.effects, width, height, false, view);
        } else {
            CarState preview;
            preview.setup = menu.setup(model);
            renderers[i]->render(asset, view.menu ? preview : car, view.menu ? orbit : camera,
                                 effects, width, height, !view.menu && night, view);
        }
        if (!view.menu) {
            const auto pauseBinding = controls.binding(static_cast<int>(Action::Pause), controls.usingGamepad());
            drawHud(ui, car, menu.playerCar(), courseName(menu.course()), pauseBinding, driving);
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
        const std::array<const char*, 8> sections{"", "Main Menu", "Car Select", "Map Select", "Settings", "", "Paused", "Garage"};
        std::string title = sections[static_cast<std::size_t>(lastScreen)];
        if (lastScreen == Screen::Drive) {
            title = std::string(carSpec(menu.playerCar()).name) + " | " + std::string(courseName(menu.course()));
        }
        if (!title.empty()) title += " | ";
        title += gameTitle;
        glfwSetWindowTitle(window.get(), title.c_str());
    }

    void reset()
    {
        car = {};
        car.setup = menu.setup(menu.playerCar());
        if (menu.course() == Course::DerelictCamp) {
            camp = CampWorld(camp.seed());
            const auto model = menu.playerCar();
            car.position = camp.spawn();
            car.heading = camp.spawnHeading();
            placeOnCampSurface(car, sceneAsset(model, static_cast<std::size_t>(model)), camp);
        }
        camera = {};
        effects.clear();
        accumulator = 0.0;
    }

    const CarAsset& sceneAsset(CarModel model, std::size_t i)
    {
        if (!renderers[i]) {
            cars[i] = loadGameCar(assets, model);
            renderers[i] = std::make_unique<Renderer>(cars[i], assets);
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
    TitleDrift titleDrift;
    GameAudio audio;
    CampWorld camp;
    CarState car;
    ChaseCamera camera;
    CameraInput cameraInput;
    Screen lastScreen = Screen::Splash;
    bool night = false;
    double previousTime = glfwGetTime();
    double accumulator = 0.0;
};

void run(const std::filesystem::path& assets, CarModel model, bool smoke, bool campTest)
{
    CarAsset asset = loadGameCar(assets, model);
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

    Window window(glfwCreateWindow(1280, 720, gameTitle, nullptr, nullptr), glfwDestroyWindow);
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
        Renderer renderer(asset, assets);
        TireEffects effects;
        if (campTest) {
            smokeTestCamp(asset, renderer, effects, window.get());
            return;
        }
        smokeTestHandling(asset);
        smokeTestGarage(asset, effects);
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
    bool campTest = false;
    try {
        CarModel model = CarModel::SlimRider;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--smoke-test") {
                smoke = true;
            } else if (argument == "--camp") {
                campTest = true;
            } else if (argument == "--gale") {
                model = CarModel::Gale;
            }
        }
#ifdef __EMSCRIPTEN__
        run("/assets", model, smoke, campTest);
#else
        const std::filesystem::path executable = std::filesystem::absolute(argv[0]);
        run(executable.parent_path() / "assets", model, smoke, campTest);
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
