#include "grass_renderer.hpp"
#include "driving.hpp"
#include "shader.hpp"
#include "shadow_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/gtc/type_ptr.hpp>

namespace {

constexpr const char* grassVertex = R"glsl(
layout (location = 0) in vec3 blade;
layout (location = 1) in vec4 rootHeight;
layout (location = 2) in vec4 shape;
layout (location = 3) in vec4 ground;
uniform mat4 uViewProjection;
uniform float uWind;
uniform bool uNight;
uniform vec4 uJetOrigins[4];
uniform vec3 uJetDirections[4];
uniform int uJetCount;
out vec3 bladeColor;

void main() {
    float tip = blade.y;
    float bend = tip * tip;
    float height = rootHeight.w;
    float s = sin(shape.y);
    float c = cos(shape.y);
    vec2 lateral = mat2(c, s, -s, c) * blade.xz * shape.x;
    float gust = sin(rootHeight.x * 0.25 + rootHeight.z * 0.19 + uWind);
    lateral += vec2(0.86, 0.51) * (0.20 + 0.16 * gust) * bend * height * (1.0 - shape.w);
    lateral += ground.xy * bend * height * shape.w * 0.99;
    float rise = tip * height * mix(1.0, 0.075, shape.w);
    for (int i = 0; i < uJetCount; ++i) {
        vec3 delta = rootHeight.xyz + vec3(0.0, height * 0.64, 0.0) - uJetOrigins[i].xyz;
        vec3 jet = uJetDirections[i];
        float distanceAlong = dot(delta, jet);
        float distanceAcross = length(delta - jet * distanceAlong);
        float spread = 0.34 + max(0.0, distanceAlong) * 0.36;
        float blast = (1.0 - smoothstep(spread * 0.13, spread, distanceAcross)) *
                      smoothstep(-0.22, 0.18, distanceAlong) *
                      (1.0 - smoothstep(0.23, 2.1, distanceAlong)) * uJetOrigins[i].w;
        float flutter = 0.84 + sin(uWind * 21.0 - distanceAlong * 12.0 + shape.y) * 0.16;
        lateral += jet.xz * blast * bend * height * flutter * 2.0;
        rise *= 1.0 - blast * bend * 0.58;
    }
    vec3 position = rootHeight.xyz + vec3(lateral.x, rise + dot(ground.zw, lateral), lateral.y);
    gl_Position = uViewProjection * vec4(position, 1.0);

    float lit = sceneShadow(rootHeight.xyz + vec3(0, 0.2, 0), 0.0015, 0);
    vec3 green = mix(vec3(0.105, 0.29, 0.069), vec3(0.29, 0.44, 0.115), shape.z);
    float brightness = (0.54 + 0.56 * tip) * (0.63 + 0.37 * lit);
    vec3 localLight = vehicleLighting(rootHeight.xyz + vec3(0.0, height * 0.5, 0.0), vec3(0, 1, 0));
    bladeColor = green * brightness * (uNight ? vec3(0.32, 0.47, 0.64) : vec3(0.84, 0.94, 0.88));
    bladeColor += green * (0.30 + 0.56 * tip) * localLight;
}
)glsl";

constexpr const char* grassFragment = R"glsl(
in vec3 bladeColor;
out vec4 pixelColor;
void main() {
    pixelColor = vec4(bladeColor, 1.0);
}
)glsl";

std::vector<glm::vec3> grassMesh(int blades, bool detailed)
{
    std::vector<glm::vec3> vertices;
    for (int i = 0; i < blades; ++i) {
        const float angle = 2.399963f * i;
        const glm::vec3 outward{std::cos(angle), 0.0f, std::sin(angle)};
        const glm::vec3 across{-outward.z, 0.0f, outward.x};
        const float height = 1.0f - i * 0.093f;
        const glm::vec3 root = outward * (i == 0 ? 0.05f : 0.26f);
        const glm::vec3 tip = root + outward * (i == 0 ? 0.31f : 0.79f) + glm::vec3(0, height, 0);
        const glm::vec3 a = root - across * 0.35f;
        const glm::vec3 b = root + across * 0.35f;
        if (detailed) {
            const glm::vec3 middle = root + outward * 0.23f + glm::vec3(0, height * 0.56f, 0);
            const glm::vec3 c = middle + across * 0.23f;
            const glm::vec3 d = middle - across * 0.23f;
            vertices.insert(vertices.end(), {a, b, c, a, c, d, d, c, tip});
        } else {
            vertices.insert(vertices.end(), {a, b, tip});
        }
    }
    return vertices;
}

glm::dvec2 tileOrigin(const CampTile& key)
{
    return {key[0] * campGrassTileSize, key[1] * campGrassTileSize};
}

} // namespace

GrassRenderer::GrassRenderer(const SceneryQuality& quality) : quality_(quality)
{
    const std::string vertex = std::string(shadowSamplingGlsl) + vehicleLightingGlsl + grassVertex;
    program_ = createShaderProgram(vertex.c_str(), grassFragment);
    viewProjection_ = glGetUniformLocation(program_, "uViewProjection");
    wind_ = glGetUniformLocation(program_, "uWind");
    night_ = glGetUniformLocation(program_, "uNight");
    jetOrigins_ = glGetUniformLocation(program_, "uJetOrigins[0]");
    jetDirections_ = glGetUniformLocation(program_, "uJetDirections[0]");
    jetCount_ = glGetUniformLocation(program_, "uJetCount");
    shadows_.initialize(program_, 0);
    vehicleLights_.initialize(program_);

    for (std::size_t i = 0; i < batches_.size(); ++i) {
        auto& batch = batches_[i];
        const auto mesh = grassMesh(static_cast<int>(i) + 2, i == 2);
        batch.vertexCount = static_cast<GLsizei>(mesh.size());
        glGenVertexArrays(1, &batch.array);
        glGenBuffers(1, &batch.vertices);
        glGenBuffers(1, &batch.instances);
        glBindVertexArray(batch.array);
        glBindBuffer(GL_ARRAY_BUFFER, batch.vertices);
        glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(glm::vec3), mesh.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, batch.instances);
        for (GLuint attr = 1; attr <= 3; ++attr) {
            glVertexAttribPointer(attr, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                                  reinterpret_cast<const void*>((attr - 1) * sizeof(glm::vec4)));
            glEnableVertexAttribArray(attr);
            glVertexAttribDivisor(attr, 1);
        }
    }
    glBindVertexArray(0);
}

GrassRenderer::~GrassRenderer()
{
    for (const auto& batch : batches_) {
        glDeleteVertexArrays(1, &batch.array);
        glDeleteBuffers(1, &batch.vertices);
        glDeleteBuffers(1, &batch.instances);
    }
    glDeleteProgram(program_);
}

void GrassRenderer::update(const CampWorld& world, const glm::vec3& player)
{
    if (world.seed() != seed_) {
        seed_ = world.seed();
        for (auto& tiles : bands_) tiles.clear();
    }
    for (auto& batch : batches_) batch.plants.clear();
    const auto center = world.origin() + glm::dvec2(player.x, player.z);
    const auto cell = [](double x) { return static_cast<std::int64_t>(std::floor(x / campGrassTileSize)); };
    const auto distance = [&](const CampTile& tile) {
        const auto origin = tileOrigin(tile);
        const auto delta = glm::clamp(center, origin, origin + campGrassTileSize) - center;
        return glm::dot(delta, delta);
    };
    const float detailedSquared = quality_.detailedGrassRadius * quality_.detailedGrassRadius;
    const bool horizon = quality_.grassDistances.back() > 0.0f;
    for (std::size_t band = 0; band < bands_.size(); ++band) {
        const auto density = static_cast<GrassBand>(band);
        const bool sparse = density >= GrassBand::Far;
        auto& tiles = bands_[band];
        const float radius = quality_.grassDistances[band];
        if (radius <= 0.0f) continue;
        const float radiusSquared = radius * radius;
        const double preload = radius + 5.0;
        const CampTile min{cell(center.x - preload), cell(center.y - preload)};
        const CampTile max{cell(center.x + preload), cell(center.y + preload)};
        for (auto it = tiles.begin(); it != tiles.end();) {
            if (distance(it->first) > (preload + 3.0) * (preload + 3.0)) it = tiles.erase(it);
            else ++it;
        }
        std::vector<CampTile> missing;
        for (auto x = min[0]; x <= max[0]; ++x) {
            for (auto z = min[1]; z <= max[1]; ++z) {
                const CampTile key{x, z};
                if (distance(key) <= preload * preload && tiles.find(key) == tiles.end()) missing.push_back(key);
            }
        }
        std::sort(missing.begin(), missing.end(), [&](const auto& a, const auto& b) {
            return distance(a) < distance(b);
        });
        // The sparse band needs less work per tile, so it can stream in larger batches.
        const std::size_t budget = density == GrassBand::Horizon ? 30 : sparse ? 14 : (tiles.empty() ? 8 : 4);
        for (std::size_t i = 0; i < std::min(budget, missing.size()); ++i) {
            const auto key = missing[i];
            tiles.emplace(key, world.buildGrass(key, quality_.grassSamples, density, horizon));
        }

        for (const auto& [key, plants] : tiles) {
            if (distance(key) >= radiusSquared) continue;
            const auto origin = tileOrigin(key);
            const auto local = world.localOrigin(origin);
            for (const auto& plant : plants) {
                const auto root = local + plant.root;
                const glm::vec2 delta(root.x - player.x, root.z - player.z);
                const float distanceSquared = glm::dot(delta, delta);
                if (distanceSquared >= radiusSquared) continue;
                const float fade = 1.0f - glm::smoothstep(radiusSquared * 0.63f, radiusSquared, distanceSquared);
                const auto track = world.grassTrack(origin + glm::dvec2(plant.root.x, plant.root.z));
                const std::size_t mesh = distanceSquared < detailedSquared ? 2 : (sparse ? 0 : 1);
                batches_[mesh].plants.push_back({glm::vec4(root, plant.height * fade),
                    {plant.width * fade, plant.angle, plant.shade, track.flattened}, {track.direction, plant.slope}});
            }
        }
    }
    for (const auto& batch : batches_) {
        if (batch.plants.empty()) continue;
        glBindBuffer(GL_ARRAY_BUFFER, batch.instances);
        glBufferData(GL_ARRAY_BUFFER, batch.plants.size() * sizeof(Instance), batch.plants.data(), GL_STREAM_DRAW);
    }
}

void GrassRenderer::draw(const glm::mat4& viewProjection, const SceneShadows& shadows,
                         const VehicleLights& lights, glm::dvec2 origin, float time, bool night, const std::vector<CarAsset::Exhaust>& outlets,
                         const glm::mat4& body, float exhaustFlow) const
{
    glUseProgram(program_);
    shadows_.bind(shadows);
    vehicleLights_.bind(lights);
    glUniformMatrix4fv(viewProjection_, 1, GL_FALSE, glm::value_ptr(viewProjection));
    constexpr double tau = 6.283185307179586;
    glUniform1f(wind_, static_cast<float>(std::remainder(origin.x * 0.25 + origin.y * 0.19 + time * 1.56, tau)));
    glUniform1i(night_, night);
    std::array<glm::vec4, 4> origins;
    std::array<glm::vec3, 4> directions;
    const int count = exhaustFlow > 0.01f ? static_cast<int>(std::min(outlets.size(), origins.size())) : 0;
    for (int i = 0; i < count; ++i) {
        const auto outlet = transformExhaust(outlets[i], body);
        origins[i] = {outlet.position, exhaustFlow};
        directions[i] = outlet.direction;
    }
    glUniform1i(jetCount_, count);
    if (count) {
        glUniform4fv(jetOrigins_, count, glm::value_ptr(origins[0]));
        glUniform3fv(jetDirections_, count, glm::value_ptr(directions[0]));
    }
    for (const auto& batch : batches_) {
        if (batch.plants.empty()) continue;
        glBindVertexArray(batch.array);
        glDrawArraysInstanced(GL_TRIANGLES, 0, batch.vertexCount, static_cast<GLsizei>(batch.plants.size()));
    }
}
