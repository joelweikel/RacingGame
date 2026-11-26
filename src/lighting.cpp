#include "lighting.hpp"
#include "shader.hpp"

#include <algorithm>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgl.h>
#endif

namespace {

constexpr int shadowResolution = 2048;

constexpr const char* shadowVertex = R"glsl(
layout (location = 0) in vec3 position;
uniform mat4 uLightMatrix;
uniform mat4 uModel;
void main() {
    gl_Position = uLightMatrix * uModel * vec4(position, 1.0);
}
)glsl";

constexpr const char* shadowFragment = R"glsl(
void main() {}
)glsl";

constexpr const char* downsampleFragment = R"glsl(
in vec2 clipPosition;
uniform sampler2D uInput;
uniform bool uExtractHighlights;
out vec4 pixelColor;

vec3 readInput(vec2 uv, vec2 texel, vec2 offset) {
    vec3 color = texture(uInput, uv + texel * offset).rgb;
    if (uExtractHighlights) {
        if (any(isnan(color)) || any(isinf(color))) {
            return vec3(0.0);
        }
        float peak = max(color.r, max(color.g, color.b));
        const float threshold = 1.02;
        const float knee = 0.30;
        float distanceToThreshold = peak - threshold;
        float transition = clamp(distanceToThreshold + knee, 0.0, 2.0 * knee);
        float energy = max(distanceToThreshold, transition * transition / (4.0 * knee));
        color *= energy / max(peak, 0.001);
    }
    return color;
}

void main() {
    vec2 uv = clipPosition * 0.5 + 0.5;
    vec2 texel = 1.0 / vec2(textureSize(uInput, 0));
    vec3 color = readInput(uv, texel, vec2(0.0)) * 0.125;
    color += (readInput(uv, texel, vec2(-2.0, -2.0)) + readInput(uv, texel, vec2( 2.0, -2.0)) +
              readInput(uv, texel, vec2(-2.0,  2.0)) + readInput(uv, texel, vec2( 2.0,  2.0))) * 0.03125;
    color += (readInput(uv, texel, vec2(-2.0, 0.0)) + readInput(uv, texel, vec2(2.0, 0.0)) +
              readInput(uv, texel, vec2(0.0, -2.0)) + readInput(uv, texel, vec2(0.0, 2.0))) * 0.0625;
    color += (readInput(uv, texel, vec2(-1.0, -1.0)) + readInput(uv, texel, vec2( 1.0, -1.0)) +
              readInput(uv, texel, vec2(-1.0,  1.0)) + readInput(uv, texel, vec2( 1.0,  1.0))) * 0.125;
    pixelColor = vec4(color, 1.0);
}
)glsl";

constexpr const char* upsampleFragment = R"glsl(
in vec2 clipPosition;
uniform sampler2D uInput;
out vec4 pixelColor;
void main() {
    vec2 uv = clipPosition * 0.5 + 0.5;
    vec2 texel = 0.85 / vec2(textureSize(uInput, 0));
    vec3 result = texture(uInput, uv).rgb * 4.0;
    result += (texture(uInput, uv + vec2(texel.x, 0.0)).rgb + texture(uInput, uv - vec2(texel.x, 0.0)).rgb +
               texture(uInput, uv + vec2(0.0, texel.y)).rgb + texture(uInput, uv - vec2(0.0, texel.y)).rgb) * 2.0;
    result += texture(uInput, uv + texel * vec2(-1.0, -1.0)).rgb + texture(uInput, uv + texel * vec2( 1.0, -1.0)).rgb +
              texture(uInput, uv + texel * vec2(-1.0,  1.0)).rgb + texture(uInput, uv + texel * vec2( 1.0,  1.0)).rgb;
    pixelColor = vec4(result * (0.55 / 16.0), 1.0);
}
)glsl";

constexpr const char* presentFragment = R"glsl(
in vec2 clipPosition;
uniform sampler2D uScene;
uniform sampler2D uGlow;
uniform sampler2D uDepth;
uniform vec3 uSunScreen;
uniform float uAspect;
uniform float uSpeedEffect;
uniform vec3 uRipple;
out vec4 pixelColor;

float clearSky(vec2 uv) {
    return step(0.999999, texture(uDepth, uv).r);
}

vec3 flare(vec2 uv) {
    vec2 sun = uSunScreen.xy;
    if (uSunScreen.z == 0.0 || any(lessThan(sun, vec2(0.0))) || any(greaterThan(sun, vec2(1.0)))) {
        return vec3(0.0);
    }
    vec2 edge = min(sun, 1.0 - sun);
    vec2 tap = vec2(2.0) / vec2(textureSize(uDepth, 0));
    float visible = (clearSky(sun) +
                     clearSky(sun + vec2(tap.x, 0.0)) + clearSky(sun - vec2(tap.x, 0.0)) +
                     clearSky(sun + vec2(0.0, tap.y)) + clearSky(sun - vec2(0.0, tap.y))) * 0.2;
    float intensity = visible * smoothstep(0.0, 0.085, min(edge.x, edge.y));
    vec2 scaled = vec2(uAspect, 1.0);
    vec2 delta = (uv - sun) * scaled;
    float width = length(delta);
    float haze = 0.21 * exp(-width * width * 53.0);
    float streak = 0.18 * exp(-abs(delta.y) * 180.0 - abs(delta.x) * 12.0);

    vec2 axis = vec2(0.5) - sun;
    float smallGhost = length((uv - (vec2(0.5) + axis * 0.53)) * scaled);
    float wideGhost = length((uv - (vec2(0.5) + axis * 1.06)) * scaled);
    float blue = 0.12 * (1.0 - smoothstep(0.013, 0.060, smallGhost));
    float red = 0.072 * exp(-abs(wideGhost - 0.051) * 83.0) * (1.0 - smoothstep(0.056, 0.12, wideGhost));
    return intensity * ((haze + streak) * vec3(1.0, 0.72, 0.42) +
                        blue * vec3(0.31, 0.66, 1.0) + red * vec3(1.0, 0.36, 0.14));
}

void main() {
    vec2 uv = clipPosition * 0.5 + 0.5;
    float ring = 0.0;
    if (uRipple.z >= 0.0 && uRipple.z < 1.0) {
        vec2 delta = (uv - uRipple.xy) * vec2(uAspect, 1.0);
        float radius = length(delta);
        float front = radius - (0.025 + 1.82 * uRipple.z);
        float envelope = exp(-front * front * 265.0) * (1.0 - uRipple.z);
        float wave = sin(front * 64.0) * envelope;
        uv += delta * (wave * 0.052 / max(radius, 0.015)) / vec2(uAspect, 1.0);
        ring = exp(-front * front * 1050.0) * (1.0 - uRipple.z);
    }
    vec3 scene = texture(uScene, uv).rgb;
    if (any(isnan(scene)) || any(isinf(scene))) {
        scene = vec3(0.094, 0.108, 0.119);
    }
    vec3 color = scene + texture(uGlow, uv).rgb * 0.40 + flare(uv) + ring * vec3(0.24, 0.61, 0.85);
    float peak = max(color.r, max(color.g, color.b));
    float highlight = max(0.0, peak - 0.82);
    float compressed = min(peak, 0.82) + 0.18 * (1.0 - exp(-highlight / 0.49));
    vec3 mapped = max(vec3(0.0), color * compressed / max(peak, 0.001));
    float edge = smoothstep(0.34, 0.72, length((uv - 0.5) * vec2(1.08, 1.0)));
    mapped *= 1.0 - 0.31 * uSpeedEffect * edge;
    pixelColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)glsl";

void allocateTexture(GLuint texture, GLint internalFormat, GLenum format, GLenum type,
                     int width, int height, GLint filter)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void checkFramebuffer(const char* error)
{
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(error);
    }
}

void bindTexture(GLenum unit, GLuint texture)
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

} // namespace

SunShadow::SunShadow()
{
    program_ = createShaderProgram(shadowVertex, shadowFragment);
    lightMatrix_ = glGetUniformLocation(program_, "uLightMatrix");
    model_ = glGetUniformLocation(program_, "uModel");
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &depth_);
    allocateTexture(depth_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    shadowResolution, shadowResolution, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    constexpr GLenum noColor = GL_NONE;
    glDrawBuffers(1, &noColor);
    glReadBuffer(GL_NONE);
    try {
        checkFramebuffer("Could not create sun shadow map");
    } catch (...) {
        glDeleteTextures(1, &depth_);
        glDeleteFramebuffers(1, &framebuffer_);
        glDeleteProgram(program_);
        throw;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SunShadow::~SunShadow()
{
    glDeleteTextures(1, &depth_);
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteProgram(program_);
}

glm::mat4 SunShadow::begin(const glm::vec3& carPosition, const glm::vec3& sunDirection)
{
    glm::mat4 view = glm::lookAt(carPosition + sunDirection * 20.0f, carPosition, glm::vec3(0.0f, 1.0f, 0.0f));
    constexpr float extent = 12.0f;
    constexpr float texel = 2.0f * extent / shadowResolution;
    view[3].x = glm::round(view[3].x / texel) * texel;
    view[3].y = glm::round(view[3].y / texel) * texel;
    const glm::mat4 lightMatrix = glm::ortho(-extent, extent, -extent, extent, 5.0f, 40.0f) * view;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, shadowResolution, shadowResolution);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.1f, 3.0f);
    glUseProgram(program_);
    glUniformMatrix4fv(lightMatrix_, 1, GL_FALSE, glm::value_ptr(lightMatrix));
    return lightMatrix;
}

void SunShadow::setModel(const glm::mat4& model) const
{
    glUniformMatrix4fv(model_, 1, GL_FALSE, glm::value_ptr(model));
}

void SunShadow::end() const
{
    glDisable(GL_POLYGON_OFFSET_FILL);
}

PostProcess::PostProcess()
{
    try {
        downsampleProgram_ = createShaderProgram(fullscreenVertex, downsampleFragment);
        upsampleProgram_ = createShaderProgram(fullscreenVertex, upsampleFragment);
        presentProgram_ = createShaderProgram(fullscreenVertex, presentFragment);
    } catch (...) {
        glDeleteProgram(downsampleProgram_);
        glDeleteProgram(upsampleProgram_);
        throw;
    }
    extractHighlights_ = glGetUniformLocation(downsampleProgram_, "uExtractHighlights");
    sunScreen_ = glGetUniformLocation(presentProgram_, "uSunScreen");
    aspect_ = glGetUniformLocation(presentProgram_, "uAspect");
    speedEffect_ = glGetUniformLocation(presentProgram_, "uSpeedEffect");
    ripple_ = glGetUniformLocation(presentProgram_, "uRipple");
    glUseProgram(presentProgram_);
    glUniform1i(glGetUniformLocation(presentProgram_, "uGlow"), 1);
    glUniform1i(glGetUniformLocation(presentProgram_, "uDepth"), 2);

    glGenFramebuffers(1, &sceneFrame_);
    glGenFramebuffers(static_cast<GLsizei>(glowFrames_.size()), glowFrames_.data());
    glGenTextures(1, &scene_);
    glGenTextures(1, &depth_);
    glGenTextures(static_cast<GLsizei>(glow_.size()), glow_.data());
    glGenVertexArrays(1, &array_);
}

PostProcess::~PostProcess()
{
    glDeleteFramebuffers(1, &sceneFrame_);
    glDeleteFramebuffers(static_cast<GLsizei>(glowFrames_.size()), glowFrames_.data());
    glDeleteTextures(1, &scene_);
    glDeleteTextures(1, &depth_);
    glDeleteTextures(static_cast<GLsizei>(glow_.size()), glow_.data());
    glDeleteVertexArrays(1, &array_);
    glDeleteProgram(downsampleProgram_);
    glDeleteProgram(upsampleProgram_);
    glDeleteProgram(presentProgram_);
}

void PostProcess::resize(int width, int height)
{
    width_ = width;
    height_ = height;
    glActiveTexture(GL_TEXTURE0);
#ifdef __EMSCRIPTEN__
    // WebGL can render to RGBA16F with this extension. Use RGBA8 otherwise.
    const bool hdr = emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(), "EXT_color_buffer_float");
    const GLint colorStorage = hdr ? GL_RGBA16F : GL_RGBA8;
    constexpr GLenum colorFormat = GL_RGBA;
    const GLenum colorType = hdr ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
#else
    constexpr GLint colorStorage = GL_RGB16F;
    constexpr GLenum colorFormat = GL_RGB;
    constexpr GLenum colorType = GL_FLOAT;
#endif
    allocateTexture(scene_, colorStorage, colorFormat, colorType, width_, height_, GL_LINEAR);
    allocateTexture(depth_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    width_, height_, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFrame_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    checkFramebuffer("Could not create HDR scene framebuffer");

    for (std::size_t i = 0; i < glow_.size(); ++i) {
        const auto divisor = 2 << i;
        const glm::ivec2 size{std::max(1, width / divisor), std::max(1, height / divisor)};
        glowSizes_[i] = size;
        allocateTexture(glow_[i], colorStorage, colorFormat, colorType, size.x, size.y, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, glowFrames_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_[i], 0);
        checkFramebuffer("Could not create bloom framebuffer");
    }
}

void PostProcess::begin(int width, int height)
{
    if (width_ != width || height_ != height) {
        resize(width, height);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFrame_);
    glViewport(0, 0, width_, height_);
}

void PostProcess::drawFullscreen() const
{
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostProcess::present(const glm::vec3& sunScreen, float speedEffect, const glm::vec3& ripple)
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(array_);
    glUseProgram(downsampleProgram_);
    for (std::size_t i = 0; i < glow_.size(); ++i) {
        glViewport(0, 0, glowSizes_[i].x, glowSizes_[i].y);
        glBindFramebuffer(GL_FRAMEBUFFER, glowFrames_[i]);
        bindTexture(GL_TEXTURE0, i == 0 ? scene_ : glow_[i - 1]);
        glUniform1i(extractHighlights_, i == 0 ? GL_TRUE : GL_FALSE);
        drawFullscreen();
    }

    glUseProgram(upsampleProgram_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (std::size_t i = glow_.size() - 1; i > 0; --i) {
        glViewport(0, 0, glowSizes_[i - 1].x, glowSizes_[i - 1].y);
        glBindFramebuffer(GL_FRAMEBUFFER, glowFrames_[i - 1]);
        bindTexture(GL_TEXTURE0, glow_[i]);
        drawFullscreen();
    }
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glUseProgram(presentProgram_);
    bindTexture(GL_TEXTURE0, scene_);
    bindTexture(GL_TEXTURE1, glow_[0]);
    bindTexture(GL_TEXTURE2, depth_);
    glUniform3fv(sunScreen_, 1, glm::value_ptr(sunScreen));
    glUniform1f(aspect_, static_cast<float>(width_) / height_);
    glUniform1f(speedEffect_, speedEffect);
    glUniform3fv(ripple_, 1, glm::value_ptr(ripple));
    drawFullscreen();
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
}
