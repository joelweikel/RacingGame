#pragma once

#include "camp.hpp"
#include "graphics.hpp"
#include "lighting.hpp"
#include "scenery_quality.hpp"

#include <map>

class GrassRenderer {
public:
    explicit GrassRenderer(const SceneryQuality& quality);
    ~GrassRenderer();
    GrassRenderer(const GrassRenderer&) = delete;
    GrassRenderer& operator=(const GrassRenderer&) = delete;

    void update(const CampWorld& world, const glm::vec3& player);
    void draw(const glm::mat4& viewProjection, const SceneShadows& shadows, glm::dvec2 origin, float time, bool night) const;

private:
    struct Instance {
        glm::vec4 rootHeight;
        glm::vec4 shape; // Width, rotation, color variation, flattening.
        glm::vec4 ground; // Track direction and terrain slope.
    };
    struct Batch {
        GLuint array = 0;
        GLuint vertices = 0;
        GLuint instances = 0;
        GLsizei vertexCount = 0;
        std::vector<Instance> plants;
    };

    SceneryQuality quality_;
    std::array<Batch, 3> batches_;
    std::array<std::map<CampTile, std::vector<CampGrassPlant>>, 4> bands_;
    std::uint32_t seed_ = 0;
    GLuint program_ = 0;
    GLint viewProjection_ = -1;
    ShadowSampling shadows_;
    GLint wind_ = -1;
    GLint night_ = -1;
};
