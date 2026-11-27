#pragma once

#include <array>

#include "graphics.hpp"
#include <glm/glm.hpp>

class SunShadow {
public:
    explicit SunShadow(int resolution = 2048);
    ~SunShadow();
    SunShadow(const SunShadow&) = delete;
    SunShadow& operator=(const SunShadow&) = delete;

    glm::mat4 begin(const glm::vec3& carPosition, const glm::vec3& lightDirection, float extent = 12.0f);
    void setModel(const glm::mat4& model) const;
    void setInstanced(bool enabled) const;
    void setMask(GLuint texture, float cutoff, float factor) const;
    void end() const;
    GLuint depthTexture() const { return depth_; }

private:
    GLuint program_ = 0;
    GLuint framebuffer_ = 0;
    GLuint depth_ = 0;
    GLint lightMatrix_ = -1;
    GLint model_ = -1;
    GLint instanced_ = -1;
    GLint alphaMask_ = -1;
    int resolution_ = 0;
};

struct SceneShadows {
    glm::mat4 nearMatrix{1.0f};
    glm::mat4 distantMatrix{1.0f};
    GLuint nearDepth = 0;
    GLuint distantDepth = 0;
};

class ShadowSampling {
public:
    void initialize(GLuint program, int nearUnit);
    void bind(const SceneShadows& shadows) const;

private:
    GLint nearMatrix_ = -1;
    GLint distantMatrix_ = -1;
    GLint hasDistant_ = -1;
    GLenum nearUnit_ = GL_TEXTURE0;
};

class PostProcess {
public:
    PostProcess();
    ~PostProcess();
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void begin(int width, int height);
    void renderCampFog(const glm::mat4& clipToWorld, const glm::vec3& eye, const SceneShadows& shadows,
                       const glm::vec3& lightDirection, glm::dvec2 origin, float groundHeight,
                       float time, bool night, float resolutionScale, int steps, float viewDistance);
    void present(const glm::vec3& sunScreen, float speedEffect, const glm::vec3& ripple, bool menu);

private:
    void resize(int width, int height);
    void drawFullscreen() const;

    int width_ = 0;
    int height_ = 0;
    static constexpr std::size_t bloomLevels = 5;
    GLuint scene_ = 0;
    GLuint depth_ = 0;
    GLuint sceneFrame_ = 0;
    std::array<glm::ivec2, bloomLevels> glowSizes_{};
    std::array<GLuint, bloomLevels> glow_{};
    std::array<GLuint, bloomLevels> glowFrames_{};
    GLuint array_ = 0;
    GLuint downsampleProgram_ = 0;
    GLuint upsampleProgram_ = 0;
    GLuint presentProgram_ = 0;
    GLuint fogProgram_ = 0;
    GLuint fogTexture_ = 0;
    GLuint fogFrame_ = 0;
    glm::ivec2 fogSize_{0};
    bool fogEnabled_ = false;
    GLint fogEnabledLocation_ = -1;
    GLint fogClip_ = -1;
    GLint fogEye_ = -1;
    ShadowSampling fogShadows_;
    GLint fogLightDirection_ = -1;
    GLint fogGroundHeight_ = -1;
    GLint fogPhase_ = -1;
    GLint fogTime_ = -1;
    GLint fogNight_ = -1;
    GLint fogSteps_ = -1;
    GLint fogDistantDensity_ = -1;
    GLint extractHighlights_ = -1;
    GLint sunScreen_ = -1;
    GLint aspect_ = -1;
    GLint speedEffect_ = -1;
    GLint ripple_ = -1;
    GLint menu_ = -1;
};
