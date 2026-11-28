#pragma once

#include "car.hpp"
#include "course.hpp"

#include <unordered_map>

struct CarState;
using CampTile = std::array<std::int64_t, 2>;
inline constexpr double campGrassTileSize = 12.0;
inline constexpr double campTerrainTileSize = 128.0;
enum class GrassBand { Near, Middle, Far, Horizon };

enum class CampProp { Tree, Timber, Shelter, Canvas, Sign, Count };

struct CampPlacement {
    CampProp kind;
    glm::vec3 center;
    glm::vec3 size;
    bool solid;
};

struct CampChunk {
    glm::dvec2 origin;
    std::array<MeshPart, 4> surfaces; // Terrain, asphalt, gravel, paint.
    std::vector<CampPlacement> props;
};

struct CampGrassPlant {
    glm::vec3 root;
    glm::vec2 slope;
    float height;
    float width;
    float angle;
    float shade;
};

struct CampGrassTrack {
    glm::vec2 direction{1.0f, 0.0f};
    float flattened = 0.0f;
};

class CampWorld {
public:
    explicit CampWorld(std::uint32_t seed = 4617u) : seed_(seed) {}

    GroundContact surface(const glm::vec3& position) const;
    glm::vec3 spawn() const;
    float spawnHeading() const;
    glm::vec3 rebase(glm::vec3& position);
    void collide(glm::vec3& position, glm::vec3& velocity) const;
    CampTile tileAt(const glm::vec3& position) const;
    glm::vec3 localOrigin(glm::dvec2 origin) const;
    CampChunk buildChunk(CampTile tile, int stride = 1) const;
    std::vector<CampGrassPlant> buildGrass(CampTile tile, int samples, GrassBand band, bool horizon = false) const;
    void trampleGrass(const CarAsset& asset, const CarState& car, float dt);
    CampGrassTrack grassTrack(glm::dvec2 position) const;
    std::uint32_t seed() const { return seed_; }
    glm::dvec2 origin() const { return origin_; }

private:
    struct Campsite {
        double x;
        double side;
        double offset;
    };

    double roadCenter(double x) const;
    double roadHeight(double x) const;
    double height(double x, double z, double offset) const;
    double meshHeight(double x, double z, glm::vec2* slope = nullptr, GroundSurface* surface = nullptr) const;
    GroundSurface surfaceMaterial(double x, double offset, const Campsite& site) const;
    Campsite campsiteAt(double x) const;
    float gravel(double x, double offset, const Campsite& site) const;
    float random(std::int64_t x, std::int64_t z, std::uint32_t salt) const;
    double noise(double x, double z, std::uint32_t salt) const;
    glm::dvec2 chunkOrigin(CampTile tile) const;
    std::vector<CampPlacement> buildProps(CampTile tile, glm::dvec2 origin, int stride = 1) const;

    struct Obstacle {
        glm::dvec2 center;
        glm::vec2 extent;
        bool round;
    };

    std::uint32_t seed_;
    glm::dvec2 origin_{0.0};
    mutable CampTile collisionTile_{};
    mutable std::vector<Obstacle> obstacles_;
    struct TrackCell {
        CampGrassTrack track;
        double pressedAt;
    };
    struct TrackCellHash {
        std::size_t operator()(const CampTile& tile) const noexcept;
    };
    std::unordered_map<CampTile, TrackCell, TrackCellHash> grassTracks_;
    std::array<glm::dvec2, 4> lastGrassContact_{};
    std::array<glm::vec2, 4> grassDirection_{};
    double grassTime_ = 0.0;
    double nextGrassCleanup_ = 1.0;
    bool hasGrassContact_ = false;
};
