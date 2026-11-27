#pragma once

#include <array>

#include "graphics.hpp"
#include <glm/glm.hpp>

class SunShadow {
public:
    SunShadow();
    ~SunShadow();
    SunShadow(const SunShadow&) = delete;
    SunShadow& operator=(const SunShadow&) = delete;

    glm::mat4 begin(const glm::vec3& carPosition, const glm::vec3& sunDirection);
    void setModel(const glm::mat4& model) const;
    void end() const;
    GLuint depthTexture() const { return depth_; }

private:
    GLuint program_ = 0;
    GLuint framebuffer_ = 0;
    GLuint depth_ = 0;
    GLint lightMatrix_ = -1;
    GLint model_ = -1;
};

class PostProcess {
public:
    PostProcess();
    ~PostProcess();
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void begin(int width, int height);
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
    GLint extractHighlights_ = -1;
    GLint sunScreen_ = -1;
    GLint aspect_ = -1;
    GLint speedEffect_ = -1;
    GLint ripple_ = -1;
    GLint menu_ = -1;
};
