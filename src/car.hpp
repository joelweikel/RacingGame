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

enum class LightControl { Always, Night, Brake };

struct Surface {
    glm::vec3 color{0.5f};
    glm::vec3 emission{0.0f};
    float metallic = 0.0f;
    float roughness = 0.65f;
    float clearcoat = 0.0f;
    float opacity = 1.0f;
    float lensTint = 0.28f;
    LightControl lightControl = LightControl::Always;
};

struct MeshPart {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Surface surface;
    bool mirrorAcrossBody = false;
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
    glm::mat4 wheelTransform{1.0f};
    // Front left, front right, rear left, rear right.
    std::array<glm::vec3, 4> hubs{};
    float wheelRadius = 0.0f;
    float wheelHalfWidth = 0.0f;
    float groundHeight = 0.0f;
    CarModel model = CarModel::SlimRider;
};

const CarSpec& carSpec(CarModel model);
CarAsset loadCar(const std::filesystem::path& path, CarModel model = CarModel::SlimRider);
std::vector<MeshPart> loadScenery(const std::filesystem::path& path);
