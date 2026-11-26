#pragma once

#include "driving.hpp"
#include "lighting.hpp"
#include "tire_effects.hpp"

#include "graphics.hpp"

struct ChaseCamera {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float recenterDelay = 0.0f;
    float speedEffect = 0.0f;
    float vibrationPhase = 0.0f;

    void update(const glm::vec2& mouseDelta, bool looking, float dt);
    void updateSpeed(float metersPerSecond, float dt);
};

enum class Course { TestGrid, DustlineOval };

struct SceneView {
    Course course = Course::TestGrid;
    bool menu = false;
    bool showCar = true;
    float time = 0.0f;
    glm::vec3 ripple{0.0f, 0.0f, -1.0f};
};

class Renderer {
public:
    explicit Renderer(const CarAsset& asset);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(const CarAsset& asset, const CarState& car, const ChaseCamera& camera,
                TireEffects& effects, int width, int height, bool night = false, const SceneView& scene = {});

private:
    class MeshBuffer {
    public:
        explicit MeshBuffer(const MeshPart& part);
        ~MeshBuffer();

        MeshBuffer(const MeshBuffer&) = delete;
        MeshBuffer& operator=(const MeshBuffer&) = delete;
        MeshBuffer(MeshBuffer&& other) noexcept;

        void draw() const;
        const Surface& surface() const { return surface_; }
        bool mirrorAcrossBody() const { return mirrorAcrossBody_; }

    private:
        GLuint vertexArray_ = 0;
        GLuint vertexBuffer_ = 0;
        GLuint indexBuffer_ = 0;
        GLsizei indexCount_ = 0;
        Surface surface_;
        bool mirrorAcrossBody_ = false;
    };

    void draw(const MeshBuffer& mesh, const glm::mat4& model, bool night, bool braking) const;

    std::vector<MeshBuffer> body_;
    std::vector<MeshBuffer> wheel_;
    MeshBuffer ground_;
    SunShadow shadow_;
    PostProcess post_;
    GLuint program_ = 0;
    GLuint skyProgram_ = 0;
    GLuint skyArray_ = 0;
    GLint model_ = -1;
    GLint viewProjection_ = -1;
    GLint color_ = -1;
    GLint surface_ = -1;
    GLint emissive_ = -1;
    GLint lensTint_ = -1;
    GLint eye_ = -1;
    GLint groundPass_ = -1;
    GLint course_ = -1;
    GLint menuLight_ = -1;
    GLint night_ = -1;
    GLint sunDirection_ = -1;
    GLint lightMatrix_ = -1;
    GLint skyClipToWorld_ = -1;
    GLint skyNight_ = -1;
    GLint skySunDirection_ = -1;
    GLint menuSky_ = -1;
    GLint menuTime_ = -1;
};
