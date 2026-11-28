#pragma once

#include "car.hpp"
#include "course.hpp"

inline constexpr int highestGear = 6;
inline constexpr float idleRpm = 850.0f;
inline constexpr float metersPerSecondToMph = 2.2369363f;

struct DriveInput {
    float throttle = 0.0f;
    float steering = 0.0f;
    bool serviceBrake = false;
    bool handbrake = false;
};

struct WheelState {
    float angularSpeed = 0.0f;
    float angle = 0.0f;
    float slip = 0.0f;
    float load = 0.0f;
    float groundTravel = 0.0f;
};

struct SuspensionState {
    float heave = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float heaveRate = 0.0f;
    float pitchRate = 0.0f;
    float rollRate = 0.0f;
};

struct CarState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float heading = 0.0f;
    float yawRate = 0.0f;
    float steering = 0.0f;
    glm::vec2 groundSlope{0.0f}; // Rise along the car's forward and left directions.
    SuspensionState suspension;
    std::array<WheelState, 4> wheels{};
    int gear = 1;
    bool automatic = true;
    bool brakeLights = false;
    float rpm = idleRpm;
    float shiftTime = 0.0f;
    CarSetup setup;
    float engineLoad = 0.0f;
    float turboSpeed = 0.0f;
    float boost = 0.0f; // Gauge pressure in bar.
    float soot = 0.0f;
    float afterfire = 0.0f;
    bool shiftFirePending = false;
};

void drive(CarState& car, const CarAsset& asset, const DriveInput& input, float dt, const CampWorld* camp = nullptr,
           Course course = Course::TestGrid);
void placeOnCampSurface(CarState& car, const CarAsset& asset, const CampWorld& camp);
glm::mat3 campGroundFrame(const CarState& car);
glm::mat4 bodyTransform(const CarAsset& asset, const CarState& car, const glm::mat4& base);
glm::mat4 vehicleTransform(const CarAsset& asset, const CarState& car, bool camp);
CarAsset::Exhaust transformExhaust(const CarAsset::Exhaust& outlet, const glm::mat4& body);
void shiftGear(CarState& car, int direction);
glm::vec3 forward(float heading);
float frontWheelSteering(const CarAsset& asset, std::size_t wheel, float steering);
