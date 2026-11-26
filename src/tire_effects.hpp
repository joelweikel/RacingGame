#pragma once

#include "driving.hpp"

#include <deque>

#include "graphics.hpp"

class TireEffects {
public:
    TireEffects();
    ~TireEffects();

    TireEffects(const TireEffects&) = delete;
    TireEffects& operator=(const TireEffects&) = delete;

    void update(const CarAsset& asset, const CarState& car, float dt);
    void clear();
    void renderMarks(const glm::mat4& viewProjection);
    void renderSmoke(const glm::mat4& viewProjection, const glm::mat4& view, const glm::vec3& eye, bool night);

    std::size_t markCount() const { return marks_.size(); }
    std::size_t smokeCount() const { return smoke_.size(); }

private:
    struct Emitter {
        glm::vec3 lastMark{0.0f};
        glm::vec3 markSide{0.0f};
        float markOpacity = 0.0f;
        float smokeCredit = 0.0f;
        bool marking = false;
    };

    struct Mark {
        glm::vec3 start;
        glm::vec3 end;
        glm::vec3 startSide;
        glm::vec3 endSide;
        float startOpacity;
        float endOpacity;
    };

    struct Smoke {
        glm::vec3 position;
        glm::vec3 velocity;
        float time = 0.0f;
        float lifetime;
        float radius;
        float strength;
        float angle;
    };

    struct EffectVertex {
        glm::vec3 position;
        glm::vec2 uv;
        float opacity;
    };

    float random();
    void beginBatch(const glm::mat4& viewProjection, bool night);
    void drawBatch(bool smoke);

    std::array<Emitter, 4> emitters_{};
    std::deque<Mark> marks_;
    std::vector<Smoke> smoke_;
    std::vector<const Smoke*> sortedSmoke_;
    std::vector<EffectVertex> vertices_;
    std::uint32_t randomState_ = 1234567u;
    GLuint program_ = 0;
    GLuint vertexArray_ = 0;
    GLuint buffer_ = 0;
    GLint viewProjection_ = -1;
    GLint smokePass_ = -1;
    GLint night_ = -1;
};
