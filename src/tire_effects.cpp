#include "tire_effects.hpp"
#include "camp.hpp"
#include "shader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/type_ptr.hpp>

namespace {

constexpr glm::vec3 up{0.0f, 1.0f, 0.0f};
constexpr std::size_t maxSmoke = 256;

constexpr const char* vertexShader = R"glsl(
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 uv;
layout (location = 2) in float opacity;
layout (location = 3) in float kind;
uniform mat4 uViewProjection;
out vec2 shape;
out float strength;
flat out float particle;
void main() {
    gl_Position = uViewProjection * vec4(position, 1.0);
    shape = uv;
    strength = opacity;
    particle = kind;
}
)glsl";

constexpr const char* fragmentShader = R"glsl(
in vec2 shape;
in float strength;
flat in float particle;
uniform int uEffectPass;
uniform bool uNight;
out vec4 pixelColor;

float lobe(vec2 center, float radius) {
    return 1.0 - smoothstep(radius * 0.15, radius, length(shape - center));
}

void main() {
    if (uEffectPass == 2) {
        float radius = dot(shape, shape);
        float opacity = exp(-radius * 2.4) * (1.0 - smoothstep(0.6, 1.0, radius));
        pixelColor = vec4(vec3(0.05), opacity * strength);
    } else if (uEffectPass == 1) {
        if (particle == 2.0) {
            float core = lobe(vec2(0.0), 0.75);
            float edge = max(core, 0.52 * lobe(vec2(0.22, -0.18), 0.67));
            vec3 heat = mix(vec3(2.8, 0.19, 0.025), vec3(6.1, 3.5, 0.65), core * core);
            pixelColor = vec4(heat, edge * strength);
            return;
        }
        float cloud = max(lobe(vec2(0.02, -0.08), 0.83), lobe(vec2(-0.33, 0.17), 0.52));
        cloud = max(cloud, 0.87 * lobe(vec2(0.29, 0.28), 0.57));
        cloud *= 0.91 + 0.09 * sin(shape.x * 13.0 + shape.y * 10.0) * cos(shape.y * 16.0);
        vec3 dark = vec3(0.37, 0.46, 0.50);
        vec3 light = vec3(0.80, 0.84, 0.88);
        if (particle == 3.0) {
            dark = vec3(0.26, 0.16, 0.08);
            light = vec3(0.54, 0.36, 0.20);
        } else if (particle == 4.0) {
            dark = vec3(0.33, 0.29, 0.23);
            light = vec3(0.59, 0.53, 0.42);
        } else if (particle == 5.0) {
            dark = vec3(0.48, 0.27, 0.11);
            light = vec3(0.77, 0.54, 0.30);
        }
        vec3 tint = mix(dark, light, clamp(shape.y + 0.55, 0.0, 1.0));
        if (uNight) {
            tint *= vec3(0.15, 0.20, 0.29);
        }
        if (particle == 1.0) tint = uNight ? vec3(0.008, 0.009, 0.011) : vec3(0.023, 0.022, 0.020);
        pixelColor = vec4(tint, cloud * strength);
    } else {
        float edge = 1.0 - smoothstep(0.64, 1.0, abs(shape.x));
        float tread = 0.87 + 0.13 * cos(shape.x * 19.0);
        pixelColor = vec4(0.003, 0.004, 0.005, edge * tread * strength);
    }
}
)glsl";

} // namespace

TireEffects::TireEffects(std::size_t markLimit, float smokeDensity)
    : markLimit_(std::max<std::size_t>(1, markLimit)), smokeDensity_(smokeDensity)
{
    smoke_.reserve(maxSmoke);
    sortedSmoke_.reserve(maxSmoke);
    vertices_.reserve(std::max(markLimit_, maxSmoke) * 6);
    program_ = createShaderProgram(vertexShader, fragmentShader);
    viewProjection_ = glGetUniformLocation(program_, "uViewProjection");
    effectPass_ = glGetUniformLocation(program_, "uEffectPass");
    night_ = glGetUniformLocation(program_, "uNight");

    glGenVertexArrays(1, &vertexArray_);
    glGenBuffers(1, &buffer_);
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EffectVertex), reinterpret_cast<const void*>(offsetof(EffectVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(EffectVertex), reinterpret_cast<const void*>(offsetof(EffectVertex, uv)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(EffectVertex), reinterpret_cast<const void*>(offsetof(EffectVertex, opacity)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(EffectVertex), reinterpret_cast<const void*>(offsetof(EffectVertex, kind)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

TireEffects::~TireEffects()
{
    glDeleteVertexArrays(1, &vertexArray_);
    glDeleteBuffers(1, &buffer_);
    glDeleteProgram(program_);
}

float TireEffects::random()
{
    randomState_ = randomState_ * 1664525u + 1013904223u;
    return static_cast<float>(randomState_ >> 8) / 16777216.0f;
}

void TireEffects::clear()
{
    emitters_ = {};
    marks_.clear();
    smoke_.clear();
    exhaustCredit_ = 0.0f;
}

void TireEffects::shiftOrigin(const glm::vec3& shift)
{
    for (auto& emitter : emitters_) emitter.lastMark -= shift;
    for (auto& mark : marks_) {
        mark.start -= shift;
        mark.end -= shift;
    }
    for (auto& puff : smoke_) puff.position -= shift;
}

void TireEffects::update(const CarAsset& asset, const CarState& car, float dt, const CampWorld* camp, Course course)
{
    const glm::vec3 front = forward(car.heading);
    const glm::vec3 side = glm::cross(front, up);
    const glm::mat3 ground = camp ? campGroundFrame(car) : glm::mat3(front, up, side);
    const float markWidth = std::max(0.065f, asset.wheelHalfWidth * 0.95f);

    for (Smoke& puff : smoke_) {
        puff.time += dt;
        puff.position += puff.velocity * dt;
        puff.velocity *= 1.0f - 0.95f * dt;
    }
    smoke_.erase(std::remove_if(smoke_.begin(), smoke_.end(), [](const Smoke& puff) {
        return puff.time >= puff.lifetime;
    }), smoke_.end());

    const float exhaust = car.setup.diesel ? car.soot : (car.afterfire > 0.0f ? 1.0f : 0.0f);
    const auto& outlets = car.setup.diesel ? asset.dieselExhaust : asset.gasolineExhaust;
    if (exhaust > 0.0f && !outlets.empty()) {
        const bool diesel = car.setup.diesel;
        const auto model = bodyTransform(asset, car, vehicleTransform(asset, car, camp != nullptr));
        exhaustCredit_ += exhaust * (diesel ? 56.0f : 110.0f) * dt;
        while (exhaustCredit_ >= 1.0f) {
            exhaustCredit_ -= 1.0f;
            for (const auto& local : outlets) {
                if (smoke_.size() >= maxSmoke + 128) break;
                const auto outlet = transformExhaust(local, model);
                const glm::vec3 scatter{random() - 0.5f, random() - 0.5f, random() - 0.5f};
                Smoke puff;
                puff.position = outlet.position + outlet.direction * 0.035f + scatter * 0.04f;
                puff.velocity = car.velocity * (diesel ? 0.24f : 0.8f) +
                               outlet.direction * (2.6f + random() * (diesel ? 2.2f : 4.2f)) +
                               up * (diesel ? 0.78f : 0.15f) + scatter * (diesel ? 1.35f : 0.7f);
                puff.lifetime = diesel ? 1.35f + random() * 1.25f : 0.065f + random() * 0.105f;
                puff.radius = diesel ? 0.16f + random() * 0.10f : 0.055f + random() * 0.055f;
                puff.strength = diesel ? 0.68f + exhaust * 0.28f : 0.87f;
                puff.angle = random() * 6.2831853f;
                puff.kind = diesel ? ParticleKind::Soot : ParticleKind::Flame;
                smoke_.push_back(puff);
            }
        }
    } else {
        exhaustCredit_ = 0.0f;
    }

    const auto onAsphalt = [&](const glm::vec3& p) {
        return groundContact(course, camp, p, asset.groundHeight).surface == GroundSurface::Asphalt;
    };
    for (std::size_t i = 0; i < car.wheels.size(); ++i) {
        Emitter& emitter = emitters_[i];
        const float slip = car.wheels[i].slip;
        const glm::vec3 offset = ground * glm::vec3(asset.hubs[i].x, car.wheels[i].groundTravel, asset.hubs[i].z);
        glm::vec3 contact = car.position + offset;
        const auto groundPatch = groundContact(course, camp, contact, asset.groundHeight);
        contact.y = groundPatch.height + (camp ? 0.022f : 0.0f);
        const bool paved = groundPatch.surface == GroundSurface::Asphalt;
        const glm::vec3 hubVelocity = car.velocity + glm::cross(up * car.yawRate, offset);
        const float rollingDust = glm::smoothstep(1.5f, 22.0f, glm::length(hubVelocity));
        if (paved && slip < 0.12f) {
            emitter.marking = false;
            emitter.smokeCredit = 0.0f;
            continue;
        }

        const float opacity = 0.24f + 0.58f * slip;
        if (!paved) {
            emitter.marking = false;
        } else if (!emitter.marking) {
            emitter.lastMark = contact;
            emitter.markSide = ground[2] * markWidth;
            emitter.markOpacity = opacity;
            emitter.marking = true;
        } else {
            const glm::vec3 movement = contact - emitter.lastMark;
            const float distance = glm::length(movement);
            if (distance >= 0.075f) {
                glm::vec3 markSide = glm::cross(movement / distance, ground[1]) * markWidth;
                if (glm::dot(emitter.markSide, markSide) < 0.0f) {
                    markSide = -markSide;
                }
                const bool stripOnAsphalt = (!camp && course == Course::TestGrid) ||
                    (onAsphalt(emitter.lastMark - emitter.markSide) && onAsphalt(emitter.lastMark + emitter.markSide) &&
                     onAsphalt(contact - markSide) && onAsphalt(contact + markSide) &&
                     onAsphalt((contact + emitter.lastMark) * 0.5f));
                if (stripOnAsphalt) {
                    if (marks_.size() == markLimit_) marks_.pop_front();
                    marks_.push_back({emitter.lastMark, contact, emitter.markSide, markSide, emitter.markOpacity, opacity});
                }
                emitter.lastMark = contact;
                emitter.markSide = markSide;
                emitter.markOpacity = opacity;
            }
        }

        const float rate = paved ? 7.0f + 34.0f * slip : 19.0f * rollingDust + 27.0f * slip;
        if (rate <= 0.0f) emitter.smokeCredit = 0.0f;
        emitter.smokeCredit += rate * smokeDensity_ * dt;
        while (emitter.smokeCredit >= 1.0f) {
            emitter.smokeCredit -= 1.0f;
            if (smoke_.size() < maxSmoke) {
                Smoke puff;
                puff.position = contact + up * 0.13f + side * (random() - 0.5f) * 0.22f;
                puff.velocity = hubVelocity * 0.12f + side * (random() - 0.5f) * 1.6f + up * (0.60f + random() * 0.80f);
                puff.lifetime = 1.10f + random() * 0.90f;
                puff.radius = 0.23f + random() * 0.17f;
                puff.strength = (0.15f + 0.31f * slip) * (0.75f + random() * 0.25f);
                puff.angle = random() * 6.2831853f;
                if (!paved) {
                    puff.radius *= 1.2f;
                    puff.strength = 0.23f + 0.22f * rollingDust + 0.24f * slip;
                    puff.kind = groundPatch.surface == GroundSurface::Sand ? ParticleKind::SandDust :
                               groundPatch.surface == GroundSurface::Gravel ? ParticleKind::GravelDust : ParticleKind::DirtDust;
                }
                smoke_.push_back(puff);
            }
        }
    }
}

void TireEffects::drawBatch(Pass pass)
{
    if (vertices_.empty()) {
        return;
    }
    glUniform1i(effectPass_, static_cast<GLint>(pass));
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(EffectVertex), vertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
}

void TireEffects::beginBatch(const glm::mat4& viewProjection, bool night)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(program_);
    glUniformMatrix4fv(viewProjection_, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform1i(night_, night ? GL_TRUE : GL_FALSE);
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
}

void TireEffects::renderContactShadow(const CarAsset& asset, const CarState& car, const glm::mat4& viewProjection)
{
    const auto front = forward(car.heading);
    const auto side = glm::cross(front, up);
    const auto wheelSpan = asset.hubs[0] - asset.hubs[2];
    const auto center = glm::vec3(car.position.x, asset.groundHeight, car.position.z) +
                        front * (asset.hubs[0].x + asset.hubs[2].x) * 0.5f;
    const auto x = front * (wheelSpan.x * 0.5f + 0.94f);
    const auto y = side * (std::abs(asset.hubs[0].z) + 0.52f);
    const EffectVertex a{center - x - y, {-1, -1}, 0.67f};
    const EffectVertex b{center + x - y, {1, -1}, 0.67f};
    const EffectVertex c{center + x + y, {1, 1}, 0.67f};
    const EffectVertex d{center - x + y, {-1, 1}, 0.67f};
    beginBatch(viewProjection, false);
    vertices_.assign({a, b, c, a, c, d});
    drawBatch(Pass::Shadow);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void TireEffects::renderMarks(const glm::mat4& viewProjection, std::size_t fadeTail, bool groundDepth)
{
    if (marks_.empty()) {
        return;
    }
    beginBatch(viewProjection, false);
    if (!groundDepth) glDisable(GL_DEPTH_TEST);
    vertices_.clear();
    const float fadeStep = fadeTail == 0 ? 0.0f : 1.0f / static_cast<float>(fadeTail);
    float fade = fadeTail == 0 ? 1.0f : 0.0f;
    for (const Mark& mark : marks_) {
        const float from = mark.startOpacity * fade;
        fade = std::min(1.0f, fade + fadeStep);
        const float to = mark.endOpacity * fade;
        const EffectVertex startLeft{mark.start - mark.startSide, {-1.0f, 0.0f}, from};
        const EffectVertex startRight{mark.start + mark.startSide, {1.0f, 0.0f}, from};
        const EffectVertex endLeft{mark.end - mark.endSide, {-1.0f, 0.0f}, to};
        const EffectVertex endRight{mark.end + mark.endSide, {1.0f, 0.0f}, to};
        vertices_.insert(vertices_.end(), {startLeft, startRight, endRight, startLeft, endRight, endLeft});
    }
    drawBatch(Pass::Marks);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void TireEffects::renderSmoke(const glm::mat4& viewProjection, const glm::mat4& view,
                              const glm::vec3& eye, bool night)
{
    if (smoke_.empty()) {
        return;
    }
    beginBatch(viewProjection, night);

    const glm::vec3 cameraSide{view[0][0], view[1][0], view[2][0]};
    const glm::vec3 cameraUp{view[0][1], view[1][1], view[2][1]};
    const glm::vec3 cameraBack{view[0][2], view[1][2], view[2][2]};
    sortedSmoke_.clear();
    for (const Smoke& puff : smoke_) {
        sortedSmoke_.push_back(&puff);
    }
    std::sort(sortedSmoke_.begin(), sortedSmoke_.end(), [&](const Smoke* a, const Smoke* b) {
        return glm::dot(a->position - eye, cameraBack) < glm::dot(b->position - eye, cameraBack);
    });

    vertices_.clear();
    for (const Smoke* puff : sortedSmoke_) {
        const float progress = puff->time / puff->lifetime;
        const bool flame = puff->kind == ParticleKind::Flame;
        const float radius = puff->radius + (flame ? 0.09f : 1.15f) * progress;
        const float fade = 1.0f - progress;
        const float opacity = puff->strength * fade * fade *
            (flame ? 1.0f : std::clamp(puff->time * 18.0f, 0.0f, 1.0f));
        const float angle = puff->angle + progress * 0.55f;
        const glm::vec3 x = (cameraSide * std::cos(angle) + cameraUp * std::sin(angle)) * radius;
        const glm::vec3 y = (cameraUp * std::cos(angle) - cameraSide * std::sin(angle)) * radius;
        const float kind = static_cast<float>(puff->kind);
        const EffectVertex bottomLeft{puff->position - x - y, {-1.0f, -1.0f}, opacity, kind};
        const EffectVertex bottomRight{puff->position + x - y, {1.0f, -1.0f}, opacity, kind};
        const EffectVertex topLeft{puff->position - x + y, {-1.0f, 1.0f}, opacity, kind};
        const EffectVertex topRight{puff->position + x + y, {1.0f, 1.0f}, opacity, kind};
        vertices_.insert(vertices_.end(), {bottomLeft, bottomRight, topRight, bottomLeft, topRight, topLeft});
    }
    drawBatch(Pass::Smoke);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
