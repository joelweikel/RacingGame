#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#undef GLAD_GL_IMPLEMENTATION

#include "renderer.hpp"
#include "shader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

constexpr glm::vec3 up{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 axle{0.0f, 0.0f, 1.0f};
constexpr float pi = 3.14159265f;
const glm::vec3 sunDirection = glm::normalize(glm::vec3(0.981f, 0.125f, 0.15f));

constexpr const char* vertexShader = R"glsl(#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 uModel;
uniform mat4 uViewProjection;

out vec3 worldPosition;
out vec3 worldNormal;

void main() {
    vec4 world = uModel * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = transpose(inverse(mat3(uModel))) * normal;
    gl_Position = uViewProjection * world;
}
)glsl";

constexpr const char* skyLighting = R"glsl(
uniform bool uNight;
uniform vec3 uSunDirection;
const vec3 horizon = vec3(0.58, 0.69, 0.74);

vec3 skyRadiance(vec3 direction) {
    float height = max(direction.y, 0.0);
    float belowHorizon = smoothstep(0.0, 0.34, -direction.y);
    if (uNight) {
        vec3 sky = mix(vec3(0.040, 0.064, 0.105), vec3(0.003, 0.008, 0.025), pow(height, 0.42));
        return mix(sky, vec3(0.007, 0.010, 0.016), belowHorizon);
    }
    vec3 sky = mix(horizon, vec3(0.047, 0.19, 0.43), pow(height, 0.45));
    float nearSun = pow(max(dot(direction, uSunDirection), 0.0), 7.0);
    sky += vec3(0.20, 0.12, 0.044) * nearSun * (1.0 - height * 0.45);
    return mix(sky, vec3(0.094, 0.108, 0.119), belowHorizon);
}
)glsl";

constexpr const char* materialFragment = R"glsl(
in vec3 worldPosition;
in vec3 worldNormal;

uniform vec3 uColor;
uniform vec3 uEye;
uniform vec4 uSurface;
uniform vec3 uEmissive;
uniform float uLensTint;
uniform bool uGroundPass;
uniform sampler2D uShadowMap;
uniform mat4 uLightMatrix;

out vec4 pixelColor;

float sunlight(vec3 normal) {
    if (uNight) {
        return 0.0;
    }
    vec3 uv = (uLightMatrix * vec4(worldPosition, 1.0)).xyz * 0.5 + 0.5;
    if (any(lessThan(uv, vec3(0.0))) || any(greaterThan(uv, vec3(1.0)))) {
        return 1.0;
    }
    float angle = max(0.0, dot(normal, uSunDirection));
    float bias = uGroundPass ? mix(0.00026, 0.00007, angle) : mix(0.0015, 0.00042, angle);
    vec2 stepSize = 1.3 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int row = -1; row <= 1; ++row) {
        for (int column = -1; column <= 1; ++column) {
            float depth = texture(uShadowMap, uv.xy + vec2(column, row) * stepSize).r;
            lit += step(uv.z - bias, depth);
        }
    }
    float edge = smoothstep(0.46, 0.49, max(abs(uv.x - 0.5), abs(uv.y - 0.5)));
    return mix(lit / 9.0, 1.0, edge);
}

void main() {
    vec3 toEye = uEye - worldPosition;
    float distanceToEye = length(toEye);
    vec3 view = toEye / max(distanceToEye, 0.0001);
    float fog = 1.0 - exp(-distanceToEye * 0.014);
    if (uGroundPass) {
        vec2 grid = worldPosition.xz / 2.5;
        vec2 width = max(fwidth(grid), vec2(0.0001));
        vec2 lines = 1.0 - smoothstep(vec2(0.0), width, abs(fract(grid + 0.5) - 0.5));
        float line = max(lines.x, lines.y) * (1.0 - smoothstep(12.0, 55.0, distanceToEye));
        vec3 asphalt = mix(uColor, vec3(0.15, 0.19, 0.205), line * 0.68);
        asphalt *= uNight ? vec3(0.19, 0.25, 0.32) : vec3(mix(0.44, 1.0, sunlight(vec3(0.0, 1.0, 0.0))));
        pixelColor = vec4(mix(asphalt, skyRadiance(-view), fog), 1.0);
        return;
    }

    float normalLengthSquared = dot(worldNormal, worldNormal);
    vec3 normal = normalLengthSquared > 1e-10
        ? worldNormal * inversesqrt(normalLengthSquared)
        : vec3(0.0, 1.0, 0.0);
    normal = faceforward(normal, -view, normal);
    float metallic = uSurface.x;
    float roughness = uSurface.y;
    float clearcoat = uSurface.z;
    float opacity = uSurface.w;

    vec3 reflection = reflect(-view, normal);
    float grazing = 1.0 - clamp(dot(normal, view), 0.0, 1.0);
    float grazingSquared = grazing * grazing;
    float fresnel = grazingSquared * grazingSquared * grazing;
    vec3 averageSky = uNight ? vec3(0.017, 0.028, 0.048) : vec3(0.36, 0.43, 0.49);
    vec3 environment = mix(skyRadiance(reflection), averageSky, roughness * 0.72);
    float direct = sunlight(normal);
    float reflectedSun = pow(max(dot(reflection, uSunDirection), 0.0), mix(160.0, 12.0, roughness));
    environment += vec3(1.4, 1.15, 0.82) * direct * reflectedSun * (1.0 - roughness * 0.64);
    if (opacity < 1.0) {
        vec3 glass = mix(environment, uColor * (uNight ? 0.23 : 1.0), uLensTint);
        pixelColor = vec4(glass, clamp(opacity * 0.65 + fresnel * 0.40, 0.0, 1.0));
        return;
    }

    float ambient = uNight ? 0.06 : 0.22 + 0.31 * (normal.y * 0.5 + 0.5);
    float daylight = ambient + 0.62 * direct * max(dot(normal, uSunDirection), 0.0);
    vec3 diffuse = uColor * daylight * (1.0 - metallic * 0.86);
    vec3 specular = mix(mix(vec3(0.045), uColor, metallic), vec3(1.0), fresnel * 0.72);
    vec3 coat = skyRadiance(reflection) * clearcoat * (0.085 + 0.18 * fresnel);
    float coatSun = pow(max(dot(reflection, uSunDirection), 0.0), 125.0);
    coat += clearcoat * direct * coatSun * vec3(1.85, 1.55, 1.18);
    vec3 shaded = diffuse + environment * specular + coat + uEmissive;
    pixelColor = vec4(mix(shaded, skyRadiance(-view), fog), 1.0);
}
)glsl";

constexpr const char* skyFragment = R"glsl(
in vec2 clipPosition;
uniform mat4 uClipToWorld;
out vec4 pixelColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 blend = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(cell), hash(cell + vec2(1.0, 0.0)), blend.x),
               mix(hash(cell + vec2(0.0, 1.0)), hash(cell + vec2(1.0, 1.0)), blend.x), blend.y);
}

void main() {
    vec3 ray = normalize((uClipToWorld * vec4(clipPosition, 1.0, 1.0)).xyz);
    vec3 sky = skyRadiance(ray);
    if (ray.y > 0.0) {
        vec2 cloudPosition = ray.xz / (ray.y + 0.13) * vec2(1.8, 1.2);
        float clouds = noise(cloudPosition) * 0.57 + noise(cloudPosition * 2.1) * 0.28 +
                       noise(cloudPosition * 4.3) * 0.15;
        float cover = smoothstep(0.49, 0.72, clouds) * smoothstep(0.015, 0.13, ray.y);
        vec3 cloudColor = uNight ? vec3(0.026, 0.038, 0.061) : vec3(0.91, 0.93, 0.95);
        sky = mix(sky, cloudColor, cover * 0.66);
        float sun = smoothstep(0.99962, 0.9998, dot(ray, uSunDirection));
        if (!uNight) {
            sky += vec3(4.8, 3.9, 2.5) * sun;
        }
    }
    pixelColor = vec4(sky, 1.0);
}
)glsl";

void compileShader(GLuint shader, const char* source)
{
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        char log[2048]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader compilation failed: ") + log);
    }
}

} // namespace

GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    const GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    const GLuint program = glCreateProgram();
    try {
        compileShader(vertex, vertexSource);
        compileShader(fragment, fragmentSource);
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);

        GLint success = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (success != GL_TRUE) {
            char log[2048]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("Shader linking failed: ") + log);
        }
    } catch (...) {
        glDeleteProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

namespace {

MeshPart makeGround(float height)
{
    constexpr float edge = 10000.0f;
    MeshPart plane;
    plane.vertices = {
        {{-edge, height, -edge}, up},
        {{-edge, height,  edge}, up},
        {{ edge, height,  edge}, up},
        {{ edge, height, -edge}, up},
    };
    plane.indices = {0, 1, 2, 0, 2, 3};
    plane.surface.color = {0.068f, 0.083f, 0.094f};
    return plane;
}

} // namespace

void ChaseCamera::update(const glm::vec2& mouseDelta, bool looking, float dt)
{
    if (looking) {
        yaw = std::remainder(yaw - mouseDelta.x * 0.006f, 2.0f * pi);
        pitch = std::clamp(pitch + mouseDelta.y * 0.0045f, -0.29f, 0.68f);
        recenterDelay = 0.65f;
        return;
    }
    const float settlingTime = std::max(0.0f, dt - recenterDelay);
    recenterDelay = std::max(0.0f, recenterDelay - dt);
    const float remaining = std::exp(-4.0f * settlingTime);
    yaw *= remaining;
    pitch *= remaining;
    if (recenterDelay == 0.0f && std::abs(yaw) < 0.001f && std::abs(pitch) < 0.001f) {
        yaw = 0.0f;
        pitch = 0.0f;
    }
}

void ChaseCamera::updateSpeed(float metersPerSecond, float dt)
{
    const float target = glm::smoothstep(23.0f, 65.0f, metersPerSecond);
    speedEffect += (target - speedEffect) * (1.0f - std::exp(-3.4f * dt));
    vibrationPhase = std::remainder(vibrationPhase + dt * (2.0f * pi), 2.0f * pi);
}

Renderer::MeshBuffer::MeshBuffer(const MeshPart& part)
    : indexCount_(static_cast<GLsizei>(part.indices.size())), surface_(part.surface),
      mirrorAcrossBody_(part.mirrorAcrossBody)
{
    glGenVertexArrays(1, &vertexArray_);
    glGenBuffers(1, &vertexBuffer_);
    glGenBuffers(1, &indexBuffer_);

    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, part.vertices.size() * sizeof(Vertex), part.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, part.indices.size() * sizeof(std::uint32_t), part.indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

Renderer::MeshBuffer::MeshBuffer(MeshBuffer&& other) noexcept
    : vertexArray_(std::exchange(other.vertexArray_, 0)),
      vertexBuffer_(std::exchange(other.vertexBuffer_, 0)),
      indexBuffer_(std::exchange(other.indexBuffer_, 0)),
      indexCount_(other.indexCount_), surface_(other.surface_), mirrorAcrossBody_(other.mirrorAcrossBody_)
{
}

Renderer::MeshBuffer::~MeshBuffer()
{
    glDeleteVertexArrays(1, &vertexArray_);
    glDeleteBuffers(1, &vertexBuffer_);
    glDeleteBuffers(1, &indexBuffer_);
}

void Renderer::MeshBuffer::draw() const
{
    glBindVertexArray(vertexArray_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

Renderer::Renderer(const CarAsset& asset) : ground_(makeGround(asset.groundHeight))
{
    body_.reserve(asset.body.size());
    for (const MeshPart& part : asset.body) {
        body_.emplace_back(part);
    }
    wheel_.reserve(asset.wheel.size());
    for (const MeshPart& part : asset.wheel) {
        wheel_.emplace_back(part);
    }

    const std::string materialCode = std::string("#version 330 core\n") + skyLighting + materialFragment;
    const std::string skyCode = std::string("#version 330 core\n") + skyLighting + skyFragment;
    program_ = createShaderProgram(vertexShader, materialCode.c_str());
    try {
        skyProgram_ = createShaderProgram(fullscreenVertex, skyCode.c_str());
    } catch (...) {
        glDeleteProgram(program_);
        throw;
    }
    glGenVertexArrays(1, &skyArray_);
    skyClipToWorld_ = glGetUniformLocation(skyProgram_, "uClipToWorld");
    skyNight_ = glGetUniformLocation(skyProgram_, "uNight");
    skySunDirection_ = glGetUniformLocation(skyProgram_, "uSunDirection");
    model_ = glGetUniformLocation(program_, "uModel");
    viewProjection_ = glGetUniformLocation(program_, "uViewProjection");
    color_ = glGetUniformLocation(program_, "uColor");
    surface_ = glGetUniformLocation(program_, "uSurface");
    emissive_ = glGetUniformLocation(program_, "uEmissive");
    lensTint_ = glGetUniformLocation(program_, "uLensTint");
    eye_ = glGetUniformLocation(program_, "uEye");
    groundPass_ = glGetUniformLocation(program_, "uGroundPass");
    night_ = glGetUniformLocation(program_, "uNight");
    sunDirection_ = glGetUniformLocation(program_, "uSunDirection");
    lightMatrix_ = glGetUniformLocation(program_, "uLightMatrix");
    glEnable(GL_DEPTH_TEST);
}

Renderer::~Renderer()
{
    glDeleteVertexArrays(1, &skyArray_);
    glDeleteProgram(skyProgram_);
    glDeleteProgram(program_);
}

void Renderer::draw(const MeshBuffer& mesh, const glm::mat4& model, bool night, bool braking) const
{
    glUniformMatrix4fv(model_, 1, GL_FALSE, glm::value_ptr(model));
    const Surface& surface = mesh.surface();
    const bool emitting = surface.lightControl == LightControl::Always ||
                          (surface.lightControl == LightControl::Night && night) ||
                          (surface.lightControl == LightControl::Brake && braking);
    const glm::vec3 emission = emitting ? surface.emission : glm::vec3(0.0f);
    glUniform3fv(color_, 1, glm::value_ptr(surface.color));
    glUniform3fv(emissive_, 1, glm::value_ptr(emission));
    glUniform1f(lensTint_, surface.lensTint);
    glUniform4f(surface_, surface.metallic, surface.roughness, surface.clearcoat, surface.opacity);
    mesh.draw();
}

void Renderer::render(const CarAsset& asset, const CarState& car, const ChaseCamera& camera,
                      TireEffects& effects, int width, int height, bool night)
{
    const glm::mat4 identity(1.0f);
    const glm::vec3 direction = forward(car.heading);
    const glm::vec3 orbit = forward(car.heading + camera.yaw);
    const float pitch = 0.35f + camera.pitch;
    constexpr float cameraDistance = 7.2f;
    const glm::vec3 focus = car.position + up * (0.45f + car.suspension.heave * 0.30f);
    const glm::vec3 cameraSide = glm::cross(orbit, up);
    const float phase = camera.vibrationPhase;
    const float sideways = 0.026f * std::sin(7.0f * phase) + 0.012f * std::sin(11.0f * phase);
    const float vertical = 0.019f * std::sin(9.0f * phase + 0.6f) + 0.008f * std::sin(13.0f * phase);
    const glm::vec3 vibration = (cameraSide * sideways + up * vertical) * camera.speedEffect;
    const glm::vec3 eye = focus - orbit * (cameraDistance * std::cos(pitch)) +
                         up * (cameraDistance * std::sin(pitch)) + vibration;
    const glm::vec3 target = focus + direction * 0.45f + vibration * 0.25f;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 view = glm::lookAt(eye, target, up);
    const float fov = 56.0f + 5.5f * camera.speedEffect;
    const glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.05f, 530.0f);
    const glm::mat4 viewProjection = projection * view;

    const glm::mat4 baseModel = glm::rotate(glm::translate(identity, car.position), car.heading, up);
    const CarSpec& spec = carSpec(asset.model);
    const float pivot = asset.groundHeight + spec.centerOfGravityHeight;
    glm::mat4 bodyModel = glm::translate(baseModel, up * (pivot + car.suspension.heave));
    bodyModel = glm::rotate(bodyModel, car.suspension.pitch, axle);
    bodyModel = glm::rotate(bodyModel, car.suspension.roll, glm::vec3(1.0f, 0.0f, 0.0f));
    bodyModel = glm::translate(bodyModel, -up * pivot);
    const glm::mat4 mirrored = glm::scale(bodyModel, glm::vec3(1.0f, 1.0f, -1.0f));
    std::array<glm::mat4, 4> wheelModels;
    for (std::size_t i = 0; i < asset.hubs.size(); ++i) {
        glm::vec3 groundHub = asset.hubs[i];
        groundHub.y = asset.groundHeight + asset.wheelRadius;
        const glm::mat4 hub = glm::translate(baseModel, groundHub);
        const glm::mat4 steering = i < 2 ? glm::rotate(hub, car.steering, up) : hub;
        const glm::mat4 roll = glm::rotate(identity, car.wheels[i].angle, axle);
        const glm::mat4 facing = i % 2 == 1 ? glm::rotate(identity, pi, up) : identity;
        wheelModels[i] = steering * roll * facing * asset.wheelTransform;
    }
    const auto drawOpaque = [&](auto&& drawPart) {
        for (const MeshBuffer& part : body_) {
            if (part.surface().opacity >= 1.0f) {
                drawPart(part, bodyModel);
                if (part.mirrorAcrossBody()) {
                    drawPart(part, mirrored);
                }
            }
        }
        for (const glm::mat4& wheelModel : wheelModels) {
            for (const MeshBuffer& part : wheel_) {
                drawPart(part, wheelModel);
            }
        }
    };
    glm::mat4 lightMatrix(1.0f);
    if (!night) {
        lightMatrix = shadow_.begin(car.position, sunDirection);
        drawOpaque([&](const MeshBuffer& part, const glm::mat4& model) {
            shadow_.setModel(model);
            part.draw();
        });
        shadow_.end();
    }

    post_.begin(width, height);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(skyProgram_);
    const glm::mat4 clipToSky = glm::inverse(projection * glm::mat4(glm::mat3(view)));
    glUniformMatrix4fv(skyClipToWorld_, 1, GL_FALSE, glm::value_ptr(clipToSky));
    glUniform1i(skyNight_, night ? GL_TRUE : GL_FALSE);
    glUniform3fv(skySunDirection_, 1, glm::value_ptr(sunDirection));
    glBindVertexArray(skyArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadow_.depthTexture());
    glUniform1i(night_, night ? GL_TRUE : GL_FALSE);
    glUniform3fv(sunDirection_, 1, glm::value_ptr(sunDirection));
    glUniformMatrix4fv(lightMatrix_, 1, GL_FALSE, glm::value_ptr(lightMatrix));
    glUniform3fv(eye_, 1, glm::value_ptr(eye));
    glUniformMatrix4fv(viewProjection_, 1, GL_FALSE, glm::value_ptr(viewProjection));

    glUniform1i(groundPass_, GL_TRUE);
    draw(ground_, identity, night, false);
    effects.renderMarks(viewProjection);
    glUseProgram(program_);
    glUniform1i(groundPass_, GL_FALSE);

    drawOpaque([&](const MeshBuffer& part, const glm::mat4& model) {
        draw(part, model, night, car.brakeLights);
    });

    const auto drawGlass = [&]() {
        for (const MeshBuffer& part : body_) {
            if (part.surface().opacity < 1.0f) {
                draw(part, bodyModel, night, car.brakeLights);
                if (part.mirrorAcrossBody()) {
                    glFrontFace(GL_CW);
                    draw(part, mirrored, night, car.brakeLights);
                    glFrontFace(GL_CCW);
                }
            }
        }
    };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glCullFace(GL_FRONT);
    drawGlass();
    glCullFace(GL_BACK);
    drawGlass();
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    effects.renderSmoke(viewProjection, view, eye, night);
    const glm::vec4 projectedSun = viewProjection * glm::vec4(sunDirection, 0.0f);
    glm::vec3 sunScreen(0.0f);
    if (!night && projectedSun.w > 0.0f) {
        sunScreen = glm::vec3(glm::vec2(projectedSun) * (0.5f / projectedSun.w) + 0.5f, 1.0f);
    }
    post_.present(sunScreen, camera.speedEffect);
    hud_.render(car, spec.torqueCurve.back().rpm, width, height);
}
