#include "driving.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr glm::vec3 up{0.0f, 1.0f, 0.0f};
constexpr float tau = 6.28318531f;
constexpr float gravity = 9.81f;
constexpr float wheelInertia = 1.45f;
constexpr float finalDrive = 3.55f;
constexpr float rpmPerRadian = 60.0f / tau;
constexpr std::array<float, highestGear> gearRatios{2.66f, 1.78f, 1.30f, 1.00f, 0.80f, 0.63f};

float approach(float current, float target, float change)
{
    return current + std::clamp(target - current, -change, change);
}

float ratioFor(int gear)
{
    const float ratio = gear < 0 ? -2.90f : gearRatios[gear - 1];
    return ratio * finalDrive;
}

float engineTorque(float rpm, const std::array<TorquePoint, 6>& torqueCurve)
{
    for (std::size_t i = 1; i < torqueCurve.size(); ++i) {
        const auto& lower = torqueCurve[i - 1];
        const auto& upper = torqueCurve[i];
        if (rpm <= upper.rpm) {
            const float blend = (rpm - lower.rpm) / (upper.rpm - lower.rpm);
            return lower.newtonMeters + blend * (upper.newtonMeters - lower.newtonMeters);
        }
    }
    return torqueCurve.back().newtonMeters;
}

void selectGear(CarState& car, int gear)
{
    if (gear != car.gear) {
        car.gear = gear;
        car.shiftTime = 0.18f;
    }
}

float updatePowertrain(CarState& car, const CarSpec& spec, float throttle, float speed, bool handbrake, float dt)
{
    car.shiftTime = std::max(0.0f, car.shiftTime - dt);
    const float frontSpeed = (car.wheels[0].angularSpeed + car.wheels[1].angularSpeed) * 0.5f;
    const float rearSpeed = (car.wheels[2].angularSpeed + car.wheels[3].angularSpeed) * 0.5f;
    const float drivenSpeed = handbrake && spec.rearDriveFraction < 1.0f
        ? frontSpeed : frontSpeed * (1.0f - spec.rearDriveFraction) + rearSpeed * spec.rearDriveFraction;
    const float drivenRpm = std::abs(drivenSpeed * ratioFor(car.gear)) * rpmPerRadian;
    const float redlineRpm = spec.torqueCurve.back().rpm;
    const float launchClutch = std::clamp(1.0f - std::abs(speed) / 7.0f, 0.0f, 1.0f);
    const float launchRpm = idleRpm + throttle * launchClutch * 1700.0f;
    const float targetRpm = std::clamp(std::max(launchRpm, drivenRpm), idleRpm, redlineRpm);
    car.rpm += (targetRpm - car.rpm) * (1.0f - std::exp(-12.0f * dt));

    if (car.automatic && car.gear > 0 && car.shiftTime == 0.0f && !handbrake) {
        if (car.rpm >= spec.upshiftRpm && speed > 10.0f && car.gear < highestGear) {
            selectGear(car, car.gear + 1);
        } else if (car.rpm < spec.downshiftRpm && car.gear > 1) {
            selectGear(car, car.gear - 1);
        }
    }
    if (car.shiftTime > 0.0f || handbrake) {
        return 0.0f;
    }

    const float limiter = std::clamp((redlineRpm + 120.0f - drivenRpm) / 420.0f, 0.0f, 1.0f);
    const float engineBraking = std::clamp((drivenRpm - 1300.0f) * 0.017f, 0.0f, 80.0f);
    const float ratio = ratioFor(car.gear);
    const float thrust = throttle * engineTorque(car.rpm, spec.torqueCurve) * limiter * ratio;
    const float drag = (1.0f - throttle) * std::copysign(engineBraking * std::abs(ratio), drivenSpeed);
    return (thrust - drag) * 0.90f;
}

float springLoad(const SuspensionState& body, const glm::vec3& hub, float staticLoad, bool rear)
{
    const float compression = -body.heave - body.pitch * hub.x + body.roll * hub.z;
    const float compressionRate = -body.heaveRate - body.pitchRate * hub.x + body.rollRate * hub.z;
    const float springRate = rear ? 37500.0f : 41500.0f;
    const float damper = rear ? 3150.0f : 3450.0f;
    const float bumpStop = std::max(0.0f, compression - 0.065f) * 125000.0f;
    return std::max(0.0f, staticLoad + springRate * compression + damper * compressionRate + bumpStop);
}

glm::vec3 tireForce(WheelState& wheel, const glm::vec3& direction, const glm::vec3& contactVelocity,
                   float radius, float load, float driveTorque, float brakeTorque, bool locked, float dt)
{
    const glm::vec3 side = glm::cross(direction, up);
    const float forwardSpeed = glm::dot(contactVelocity, direction);
    const float sideSpeed = glm::dot(contactVelocity, side);
    const float slipReference = std::max(2.5f, std::abs(forwardSpeed));
    const float longStiffness = 14500.0f / slipReference;
    float freeSpin = wheel.angularSpeed + driveTorque * dt / wheelInertia;
    freeSpin = approach(freeSpin, 0.0f, brakeTorque * dt / wheelInertia);

    // Solve longitudinal tire and wheel inertia coupling implicitly to stay stable near zero speed.
    const float coupling = 1.0f + longStiffness * radius * radius * dt / wheelInertia;
    const float slipAngle = std::atan2(sideSpeed, slipReference);
    glm::vec2 force{
        longStiffness * (radius * freeSpin - forwardSpeed) / coupling,
        -32000.0f * slipAngle,
    };
    if (locked) {
        force = -glm::vec2(forwardSpeed, sideSpeed) * longStiffness;
    }
    const float magnitude = glm::length(force);
    const float grip = (locked ? 0.72f : 1.15f) * load;
    if (magnitude > grip) {
        force *= grip / magnitude;
    }

    wheel.angularSpeed = locked ? 0.0f : freeSpin - force.x * radius * dt / wheelInertia;
    wheel.angle = std::remainder(wheel.angle - wheel.angularSpeed * dt, tau);
    const float slipSpeed = glm::length(glm::vec2(radius * wheel.angularSpeed - forwardSpeed, sideSpeed));
    const float sliding = std::clamp((slipSpeed - 0.65f) / 4.0f, 0.0f, 1.0f);
    const float gripUse = magnitude / std::max(1.0f, grip);
    const float loaded = std::clamp((gripUse - 0.72f) * 3.6f, 0.0f, 1.0f) *
                         std::clamp(load / 900.0f, 0.0f, 1.0f);
    wheel.slip = sliding * loaded;
    return direction * force.x + side * force.y;
}

} // namespace

glm::vec3 forward(float heading)
{
    return {std::cos(heading), 0.0f, -std::sin(heading)};
}

void shiftGear(CarState& car, int direction)
{
    car.automatic = false;
    if (car.gear > 0) {
        selectGear(car, std::clamp(car.gear + direction, 1, highestGear));
    }
}

void drive(CarState& car, const CarAsset& asset, const DriveInput& input, float dt)
{
    const CarSpec& spec = carSpec(asset.model);
    const glm::vec3 direction = forward(car.heading);
    const glm::vec3 side = glm::cross(direction, up);
    const float forwardSpeed = glm::dot(car.velocity, direction);
    const float speed = glm::length(car.velocity);
    // Steering input remains controllable with a digital keyboard at higher speeds.
    const float steeringLimit = 0.54f / (1.0f + std::abs(forwardSpeed) * 0.055f);
    car.steering = approach(car.steering, input.steering * steeringLimit, 2.5f * dt);

    constexpr float reverseThreshold = 0.6f;
    const bool oppositeGear = input.throttle * static_cast<float>(car.gear) < 0.0f;
    const bool directionBrake = input.throttle * forwardSpeed < -reverseThreshold ||
                                (oppositeGear && speed >= reverseThreshold);
    const bool serviceBrake = input.serviceBrake || directionBrake;
    car.brakeLights = serviceBrake || input.handbrake;
    if (oppositeGear && !serviceBrake) {
        selectGear(car, input.throttle < 0.0f ? -1 : 1);
    }
    const bool gearMatches = input.throttle * static_cast<float>(car.gear) > 0.0f;
    const float throttle = !serviceBrake && gearMatches ? std::abs(input.throttle) : 0.0f;
    const float driveTorque = updatePowertrain(car, spec, throttle, forwardSpeed, input.handbrake, dt);

    const float wheelbase = asset.hubs[0].x - asset.hubs[2].x;
    const float weight = spec.mass * gravity;
    const float frontStaticLoad = -asset.hubs[2].x / wheelbase * weight;

    glm::vec3 tireForces(0.0f);
    float verticalForce = -weight;
    float pitchTorque = 0.0f;
    float rollTorque = 0.0f;
    float yawTorque = -car.yawRate * 70.0f;
    for (std::size_t i = 0; i < car.wheels.size(); ++i) {
        const bool rear = i >= 2;
        const glm::vec3& hub = asset.hubs[i];
        const glm::vec3 offset = direction * hub.x + side * hub.z;
        const glm::vec3 contactVelocity = car.velocity + glm::cross(up * car.yawRate, offset);
        const glm::vec3 wheelDirection = rear ? direction : forward(car.heading + car.steering);
        const float staticLoad = (rear ? weight - frontStaticLoad : frontStaticLoad) * 0.5f;
        const float load = springLoad(car.suspension, hub, staticLoad, rear);
        car.wheels[i].load = load;
        const float brakeTorque = serviceBrake ? (rear ? 880.0f : 1500.0f) : 0.0f;
        const float axleDriveFraction = rear ? spec.rearDriveFraction : 1.0f - spec.rearDriveFraction;
        const glm::vec3 force = tireForce(car.wheels[i], wheelDirection, contactVelocity, asset.wheelRadius,
                                        load, driveTorque * axleDriveFraction * 0.5f,
                                        brakeTorque, rear && input.handbrake, dt);
        tireForces += force;
        yawTorque += glm::cross(offset, force).y;
        verticalForce += load;
        pitchTorque += hub.x * load;
        rollTorque -= hub.z * load;
    }

    // Forces at the contact patches also pitch and roll the sprung body about its center of gravity.
    pitchTorque += spec.centerOfGravityHeight * glm::dot(tireForces, direction);
    rollTorque -= spec.centerOfGravityHeight * glm::dot(tireForces, side);
    SuspensionState& body = car.suspension;
    body.heaveRate += verticalForce / spec.mass * dt;
    body.pitchRate += pitchTorque / spec.inertia.y * dt;
    body.rollRate += rollTorque / spec.inertia.z * dt;
    body.heave += body.heaveRate * dt;
    body.pitch += body.pitchRate * dt;
    body.roll += body.rollRate * dt;

    const glm::vec3 drag = car.velocity * (14.0f + 0.44f * speed);
    const glm::vec3 acceleration = (tireForces - drag) / spec.mass;
    car.velocity += acceleration * dt;
    car.yawRate += yawTorque / spec.inertia.x * dt;
    if (throttle == 0.0f && glm::dot(car.velocity, car.velocity) < 0.0025f && std::abs(car.yawRate) < 0.025f) {
        car.velocity = glm::vec3(0.0f);
        car.yawRate = 0.0f;
    }
    car.position += car.velocity * dt;
    car.heading = std::remainder(car.heading + car.yawRate * dt, tau);
}
