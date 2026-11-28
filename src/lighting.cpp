#include "lighting.hpp"
#include "shader.hpp"
#include "shadow_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgl.h>
#endif

namespace {

constexpr const char* shadowVertex = R"glsl(
layout (location = 0) in vec3 position;
layout (location = 2) in vec3 instanceCenter;
layout (location = 3) in vec3 instanceSize;
layout (location = 4) in vec2 uv;
out vec2 textureUV;
uniform mat4 uLightMatrix;
uniform mat4 uModel;
uniform bool uInstanced;
void main() {
    vec3 local = uInstanced ? position * instanceSize + instanceCenter : position;
    textureUV = uv;
    gl_Position = uLightMatrix * uModel * vec4(local, 1.0);
}
)glsl";

constexpr const char* shadowFragment = R"glsl(
in vec2 textureUV;
uniform sampler2D uBaseColor;
uniform vec2 uAlphaMask;
void main() {
    if (uAlphaMask.x >= 0.0 && texture(uBaseColor, textureUV).a * uAlphaMask.y < uAlphaMask.x) discard;
}
)glsl";

constexpr const char* campFogFragment = R"glsl(
in vec2 clipPosition;
uniform sampler2D uDepth;
uniform mat4 uClipToWorld;
uniform vec3 uEye;
uniform vec3 uLightDirection;
uniform float uGroundHeight;
uniform vec2 uFogPhase;
uniform float uTime;
uniform bool uNight;
uniform int uSteps;
uniform float uDistantDensity;
out vec4 pixelColor;

void main() {
    vec2 uv = clipPosition * 0.5 + 0.5;
    float depth = texture(uDepth, uv).r;
    vec4 world = uClipToWorld * vec4(clipPosition, depth * 2.0 - 1.0, 1.0);
    vec3 offset = world.xyz / world.w - uEye;
    float distanceToSurface = length(offset);
    vec3 ray = offset / max(distanceToSurface, 0.001);
    float march = min(distanceToSurface, 118.0);
    float stride = march / float(uSteps);
    float jitter = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715))));
    float phase = 0.28 + 1.85 * pow(max(dot(ray, uLightDirection), 0.0), 11.0);
    vec3 fill = uNight ? vec3(0.053, 0.078, 0.116) : vec3(0.40, 0.49, 0.54);
    vec3 key = uNight ? vec3(0.16, 0.24, 0.39) : vec3(0.23, 0.20, 0.16);
    vec3 light = vec3(0.0);
    float transmission = 1.0;
    for (int i = 0; i < 28; ++i) {
        if (i >= uSteps) break;
        vec3 point = uEye + ray * stride * (float(i) + jitter);
        float bank = sin(point.x * 0.033 + point.z * 0.050 + uFogPhase.x + uTime * 0.20) *
                     sin(point.z * 0.094 - point.x * 0.015 + uFogPhase.y - uTime * 0.13);
        float altitude = max(point.y - uGroundHeight - 1.7 - bank * 2.5, 0.0);
        float density = 0.0039 + (0.0115 + bank * 0.0055) * exp(-altitude * 0.15);
        float sampleTransmission = exp(-density * stride);
        light += transmission * (1.0 - sampleTransmission) *
                 (fill + key * phase * sceneShadow(point, 0.00086, 1));
        transmission *= sampleTransmission;
    }
    float distant = exp(-max(distanceToSurface - march, 0.0) * uDistantDensity);
    light += transmission * (1.0 - distant) * fill;
    pixelColor = vec4(light, transmission * distant);
}
)glsl";

constexpr const char* reflectionFragment = R"glsl(
in vec2 clipPosition;
uniform sampler2D uColorBuffer;
uniform sampler2D uDepthBuffer;
uniform vec2 uProjectionDepth;
uniform float uLogRange;
out vec4 pixelColor;

void main() {
    vec2 uv = clipPosition * 0.5 + 0.5;
    float depth = texture(uDepthBuffer, uv).r;
    float viewDepth = uProjectionDepth.y / (depth * 2.0 - 1.0 + uProjectionDepth.x);
    float encoded = depth >= 1.0 ? 1.0 : log2(1.0 + viewDepth) / uLogRange;
    pixelColor = vec4(texture(uColorBuffer, uv).rgb, encoded);
}
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
uniform bool uBloom;
uniform sampler2D uDepth;
uniform sampler2D uFog;
uniform bool uFogEnabled;
uniform vec3 uSunScreen;
uniform float uAspect;
uniform float uSpeedEffect;
uniform vec3 uRipple;
uniform bool uMenu;
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

float compressPeak(float peak) {
    float highlight = max(0.0, peak - 0.82);
    return min(peak, 0.82) + 0.18 * (1.0 - exp(-highlight / 0.49));
}

vec4 upsampleFog(vec2 uv) {
    vec2 size = vec2(textureSize(uFog, 0));
    vec2 pixel = uv * size - 0.5;
    vec2 fraction = fract(pixel);
    vec2 base = (floor(pixel) + 0.5) / size;
    float depth = max(1e-6, 1.0 - texture(uDepth, uv).r);
    vec4 fog = vec4(0.0);
    float sum = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec2 tap = base + vec2(float(x), float(y)) / size;
            float sampleDepth = max(1e-6, 1.0 - texture(uDepth, tap).r);
            vec2 weight = vec2(x == 0 ? 1.0 - fraction.x : fraction.x,
                               y == 0 ? 1.0 - fraction.y : fraction.y);
            float blend = weight.x * weight.y * exp(-8.0 * abs(log2(sampleDepth / depth)));
            fog += texture(uFog, tap) * blend;
            sum += blend;
        }
    }
    return sum > 0.00001 ? fog / sum : texture(uFog, uv);
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
    if (uFogEnabled) {
        vec4 fog = upsampleFog(uv);
        scene = scene * fog.a + fog.rgb;
    }
    vec3 glow = uBloom ? texture(uGlow, uv).rgb * 0.40 : vec3(0.0);
    vec3 color = scene + glow + flare(uv) + ring * vec3(0.24, 0.61, 0.85);
    float peak = max(color.r, max(color.g, color.b));
    vec3 mapped = max(vec3(0.0), color * compressPeak(peak) / max(peak, 0.001));
    if (uMenu) mapped /= compressPeak(1.0);
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

SunShadow::SunShadow(int resolution)
{
    GLint maxTexture = 0;
    GLint maxViewport[2]{};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
    resolution_ = std::min({resolution, maxTexture, maxViewport[0], maxViewport[1]});
    program_ = createShaderProgram(shadowVertex, shadowFragment);
    lightMatrix_ = glGetUniformLocation(program_, "uLightMatrix");
    model_ = glGetUniformLocation(program_, "uModel");
    instanced_ = glGetUniformLocation(program_, "uInstanced");
    alphaMask_ = glGetUniformLocation(program_, "uAlphaMask");
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uBaseColor"), 4);
    glGenFramebuffers(1, &framebuffer_);
    glGenTextures(1, &depth_);
    allocateTexture(depth_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    resolution_, resolution_, GL_NEAREST);
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

glm::mat4 SunShadow::begin(const glm::vec3& carPosition, const glm::vec3& lightDirection, float extent)
{
    const float distance = extent * (20.0f / 12.0f);
    glm::mat4 view = glm::lookAt(carPosition + lightDirection * distance, carPosition, glm::vec3(0.0f, 1.0f, 0.0f));
    const float texel = 2.0f * extent / resolution_;
    view[3].x = glm::round(view[3].x / texel) * texel;
    view[3].y = glm::round(view[3].y / texel) * texel;
    const glm::mat4 lightMatrix = glm::ortho(-extent, extent, -extent, extent, 5.0f, distance * 2.0f) * view;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, resolution_, resolution_);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.1f, 3.0f);
    glUseProgram(program_);
    setInstanced(false);
    glUniformMatrix4fv(lightMatrix_, 1, GL_FALSE, glm::value_ptr(lightMatrix));
    return lightMatrix;
}

void SunShadow::setInstanced(bool enabled) const
{
    glUniform1i(instanced_, enabled);
}

void SunShadow::setMask(GLuint texture, float cutoff, float factor) const
{
    bindTexture(GL_TEXTURE4, texture);
    glUniform2f(alphaMask_, cutoff, factor);
}

void SunShadow::setModel(const glm::mat4& model) const
{
    glUniformMatrix4fv(model_, 1, GL_FALSE, glm::value_ptr(model));
}

void SunShadow::end() const
{
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void ShadowSampling::initialize(GLuint program, int nearUnit)
{
    nearUnit_ = GL_TEXTURE0 + nearUnit;
    nearMatrix_ = glGetUniformLocation(program, "uLightMatrix");
    distantMatrix_ = glGetUniformLocation(program, "uDistantLightMatrix");
    hasDistant_ = glGetUniformLocation(program, "uHasDistantShadow");
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uShadowMap"), nearUnit);
    glUniform1i(glGetUniformLocation(program, "uDistantShadowMap"), 5);
}

void ShadowSampling::bind(const SceneShadows& shadows) const
{
    bindTexture(nearUnit_, shadows.nearDepth);
    bindTexture(GL_TEXTURE5, shadows.distantDepth ? shadows.distantDepth : shadows.nearDepth);
    glUniformMatrix4fv(nearMatrix_, 1, GL_FALSE, glm::value_ptr(shadows.nearMatrix));
    glUniformMatrix4fv(distantMatrix_, 1, GL_FALSE, glm::value_ptr(shadows.distantMatrix));
    glUniform1i(hasDistant_, shadows.distantDepth != 0);
    glActiveTexture(GL_TEXTURE0);
}

PostProcess::PostProcess()
{
    try {
        downsampleProgram_ = createShaderProgram(fullscreenVertex, downsampleFragment);
        upsampleProgram_ = createShaderProgram(fullscreenVertex, upsampleFragment);
        presentProgram_ = createShaderProgram(fullscreenVertex, presentFragment);
        const std::string fogCode = std::string(shadowSamplingGlsl) + campFogFragment;
        fogProgram_ = createShaderProgram(fullscreenVertex, fogCode.c_str());
        reflectionProgram_ = createShaderProgram(fullscreenVertex, reflectionFragment);
    } catch (...) {
        glDeleteProgram(downsampleProgram_);
        glDeleteProgram(upsampleProgram_);
        glDeleteProgram(presentProgram_);
        glDeleteProgram(fogProgram_);
        throw;
    }
    extractHighlights_ = glGetUniformLocation(downsampleProgram_, "uExtractHighlights");
    sunScreen_ = glGetUniformLocation(presentProgram_, "uSunScreen");
    aspect_ = glGetUniformLocation(presentProgram_, "uAspect");
    speedEffect_ = glGetUniformLocation(presentProgram_, "uSpeedEffect");
    ripple_ = glGetUniformLocation(presentProgram_, "uRipple");
    menu_ = glGetUniformLocation(presentProgram_, "uMenu");
    glUseProgram(presentProgram_);
    glUniform1i(glGetUniformLocation(presentProgram_, "uGlow"), 1);
    bloom_ = glGetUniformLocation(presentProgram_, "uBloom");
    glUniform1i(glGetUniformLocation(presentProgram_, "uDepth"), 2);
    glUniform1i(glGetUniformLocation(presentProgram_, "uFog"), 3);
    fogEnabledLocation_ = glGetUniformLocation(presentProgram_, "uFogEnabled");
    fogShadows_.initialize(fogProgram_, 1);
    fogClip_ = glGetUniformLocation(fogProgram_, "uClipToWorld");
    fogEye_ = glGetUniformLocation(fogProgram_, "uEye");
    fogLightDirection_ = glGetUniformLocation(fogProgram_, "uLightDirection");
    fogGroundHeight_ = glGetUniformLocation(fogProgram_, "uGroundHeight");
    fogPhase_ = glGetUniformLocation(fogProgram_, "uFogPhase");
    fogTime_ = glGetUniformLocation(fogProgram_, "uTime");
    fogNight_ = glGetUniformLocation(fogProgram_, "uNight");
    fogSteps_ = glGetUniformLocation(fogProgram_, "uSteps");
    fogDistantDensity_ = glGetUniformLocation(fogProgram_, "uDistantDensity");
    glUseProgram(reflectionProgram_);
    glUniform1i(glGetUniformLocation(reflectionProgram_, "uColorBuffer"), 0);
    glUniform1i(glGetUniformLocation(reflectionProgram_, "uDepthBuffer"), 1);
    reflectionDepth_ = glGetUniformLocation(reflectionProgram_, "uProjectionDepth");
    reflectionLogRange_ = glGetUniformLocation(reflectionProgram_, "uLogRange");

    glGenFramebuffers(1, &sceneFrame_);
    glGenFramebuffers(1, &fogFrame_);
    glGenFramebuffers(1, &reflectionFrame_);
    glGenFramebuffers(static_cast<GLsizei>(glowFrames_.size()), glowFrames_.data());
    glGenTextures(1, &scene_);
    glGenTextures(1, &depth_);
    glGenTextures(1, &fogTexture_);
    glGenTextures(1, &reflectionTexture_);
    glGenTextures(static_cast<GLsizei>(glow_.size()), glow_.data());
    glGenVertexArrays(1, &array_);
}

PostProcess::~PostProcess()
{
    glDeleteFramebuffers(1, &sceneFrame_);
    glDeleteFramebuffers(1, &fogFrame_);
    glDeleteFramebuffers(1, &reflectionFrame_);
    glDeleteFramebuffers(static_cast<GLsizei>(glowFrames_.size()), glowFrames_.data());
    glDeleteTextures(1, &scene_);
    glDeleteTextures(1, &depth_);
    glDeleteTextures(1, &fogTexture_);
    glDeleteTextures(1, &reflectionTexture_);
    glDeleteTextures(static_cast<GLsizei>(glow_.size()), glow_.data());
    glDeleteVertexArrays(1, &array_);
    glDeleteProgram(downsampleProgram_);
    glDeleteProgram(upsampleProgram_);
    glDeleteProgram(presentProgram_);
    glDeleteProgram(fogProgram_);
    glDeleteProgram(reflectionProgram_);
}

void PostProcess::resize(int width, int height)
{
    width_ = width;
    height_ = height;
    glActiveTexture(GL_TEXTURE0);
#ifdef __EMSCRIPTEN__
    // WebGL can render to RGBA16F with this extension. Use RGBA8 otherwise.
    hdr_ = emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(), "EXT_color_buffer_float");
    const GLint colorStorage = hdr_ ? GL_RGBA16F : GL_RGBA8;
    constexpr GLenum colorFormat = GL_RGBA;
    const GLenum colorType = hdr_ ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
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

void PostProcess::begin(int width, int height, float scale)
{
    fogEnabled_ = false;
    outputWidth_ = width;
    outputHeight_ = height;
    const int sceneWidth = std::max(1, static_cast<int>(width * scale));
    const int sceneHeight = std::max(1, static_cast<int>(height * scale));
    if (width_ != sceneWidth || height_ != sceneHeight) {
        resize(sceneWidth, sceneHeight);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFrame_);
    glViewport(0, 0, width_, height_);
}

GLuint PostProcess::captureReflections(const glm::mat4& projection, float logRange)
{
    // Capture the opaque surroundings before drawing the vehicle. Alpha stores log view depth.
    const glm::ivec2 size{std::max(1, width_ / 2), std::max(1, height_ / 2)};
    glBindFramebuffer(GL_FRAMEBUFFER, reflectionFrame_);
    if (reflectionSize_ != size) {
        reflectionSize_ = size;
        glActiveTexture(GL_TEXTURE0);
        allocateTexture(reflectionTexture_, hdr_ ? GL_RGBA16F : GL_RGBA8, GL_RGBA,
                        hdr_ ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, size.x, size.y, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflectionTexture_, 0);
        checkFramebuffer("Could not create car reflection framebuffer");
    }
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, size.x, size.y);
    glBindVertexArray(array_);
    glUseProgram(reflectionProgram_);
    glUniform2f(reflectionDepth_, projection[2][2], projection[3][2]);
    glUniform1f(reflectionLogRange_, logRange);
    bindTexture(GL_TEXTURE0, scene_);
    bindTexture(GL_TEXTURE1, depth_);
    drawFullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFrame_);
    glViewport(0, 0, width_, height_);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    return reflectionTexture_;
}

void PostProcess::renderCampFog(const glm::mat4& clipToWorld, const glm::vec3& eye,
                               const SceneShadows& shadows, const glm::vec3& lightDirection,
                               glm::dvec2 origin, float groundHeight, float time, bool night,
                               float resolutionScale, int steps, float viewDistance)
{
    fogEnabled_ = true;
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, fogFrame_);
    const float scale = std::min({resolutionScale, 960.0f / width_, 540.0f / height_});
    const glm::ivec2 size{std::max(1, static_cast<int>(width_ * scale)),
                          std::max(1, static_cast<int>(height_ * scale))};
    if (size != fogSize_) {
        fogSize_ = size;
        glActiveTexture(GL_TEXTURE0);
        allocateTexture(fogTexture_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, size.x, size.y, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fogTexture_, 0);
        checkFramebuffer("Could not create camp fog framebuffer");
    }
    glViewport(0, 0, size.x, size.y);
    glBindVertexArray(array_);
    glUseProgram(fogProgram_);
    glUniformMatrix4fv(fogClip_, 1, GL_FALSE, glm::value_ptr(clipToWorld));
    glUniform3fv(fogEye_, 1, glm::value_ptr(eye));
    fogShadows_.bind(shadows);
    glUniform3fv(fogLightDirection_, 1, glm::value_ptr(lightDirection));
    glUniform1f(fogGroundHeight_, groundHeight);
    constexpr double tau = 6.283185307179586;
    glUniform2f(fogPhase_, static_cast<float>(std::remainder(origin.x * 0.033 + origin.y * 0.050, tau)),
                 static_cast<float>(std::remainder(origin.y * 0.094 - origin.x * 0.015, tau)));
    glUniform1f(fogTime_, time);
    glUniform1i(fogNight_, night);
    glUniform1i(fogSteps_, steps);
    glUniform1f(fogDistantDensity_, 0.0045f * 530.0f / viewDistance);
    bindTexture(GL_TEXTURE0, depth_);
    drawFullscreen();
}

void PostProcess::drawFullscreen() const
{
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostProcess::present(const glm::vec3& sunScreen, float speedEffect, const glm::vec3& ripple, bool menu, bool bloom)
{
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(array_);
    if (bloom) {
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
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth_, outputHeight_);
    glUseProgram(presentProgram_);
    bindTexture(GL_TEXTURE0, scene_);
    bindTexture(GL_TEXTURE1, glow_[0]);
    bindTexture(GL_TEXTURE2, depth_);
    bindTexture(GL_TEXTURE3, fogEnabled_ ? fogTexture_ : scene_);
    glUniform1i(fogEnabledLocation_, fogEnabled_);
    glUniform1i(bloom_, bloom);
    glUniform3fv(sunScreen_, 1, glm::value_ptr(sunScreen));
    glUniform1f(aspect_, static_cast<float>(outputWidth_) / outputHeight_);
    glUniform1f(speedEffect_, speedEffect);
    glUniform3fv(ripple_, 1, glm::value_ptr(ripple));
    glUniform1i(menu_, menu ? GL_TRUE : GL_FALSE);
    drawFullscreen();
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
}
