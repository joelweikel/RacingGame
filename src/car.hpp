#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv{0.0f};
};

enum class LightControl { Always, Headlamp, Night, Brake };
enum class CarPart { Always, HoodIntake, Wing, Splitter, Gasoline, Diesel };

struct CarSetup {
    glm::ivec3 paint{-1}; // -1 uses the authored color. Custom channels use sRGB bytes.
    bool hoodIntake = false;
    bool wing = false;
    bool splitter = false;
    bool diesel = false;

    bool shows(CarPart part) const;
};

struct Surface {
    glm::vec3 color{0.5f};
    glm::vec3 emission{0.0f};
    float metallic = 0.0f;
    float roughness = 0.65f;
    float clearcoat = 0.0f;
    float opacity = 1.0f;
    float lensTint = 0.28f;
    LightControl lightControl = LightControl::Always;
    bool paint = false;
};

struct MeshPart {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Surface surface;
    bool mirrorAcrossBody = false;
    CarPart carPart = CarPart::Always;
    struct Image {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
    };
    std::shared_ptr<const Image> baseColor;
    float alphaFactor = 1.0f;
    float alphaCutoff = -1.0f;
};

enum class CarModel { SlimRider, Gale };

struct TorquePoint {
    float rpm;
    float newtonMeters;
};

struct CarSpec {
    std::string_view name;
    const char* assetFile;
    float wheelbase;
    float wheelInset;
    float mass;
    glm::vec3 inertia; // Yaw, pitch, roll.
    float centerOfGravityHeight;
    float frontWeightFraction;
    float rearDriveFraction;
    float upshiftRpm;
    float downshiftRpm;
    std::array<TorquePoint, 6> torqueCurve;
};

struct CarAsset {
    std::vector<MeshPart> body;
    std::vector<MeshPart> wheel;
    std::vector<MeshPart> brakeDisc;
    std::vector<MeshPart> brakeCaliper;
    glm::mat4 wheelTransform{1.0f};
    struct Exhaust {
        glm::vec3 position;
        glm::vec3 direction;
    };
    std::vector<Exhaust> gasolineExhaust;
    std::vector<Exhaust> dieselExhaust;
    // Front left, front right, rear left, rear right.
    std::array<glm::vec3, 4> hubs{};
    std::array<glm::vec3, 2> headlights{};
    std::array<glm::vec3, 2> taillights{};
    float wheelRadius = 0.0f;
    float wheelHalfWidth = 0.0f;
    float groundHeight = 0.0f;
    CarModel model = CarModel::SlimRider;
};

const CarSpec& carSpec(CarModel model);
const CarSpec& powertrainSpec(CarModel model, bool diesel);
CarAsset loadCar(const std::filesystem::path& path, CarModel model = CarModel::SlimRider);
CarAsset loadGameCar(const std::filesystem::path& assets, CarModel model);
std::vector<MeshPart> loadScenery(const std::filesystem::path& path);
