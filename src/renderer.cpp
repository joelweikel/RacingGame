#ifndef __EMSCRIPTEN__
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#undef GLAD_GL_IMPLEMENTATION
#endif

#include "renderer.hpp"
#include "camp.hpp"
#include "grass_renderer.hpp"
#include "shader.hpp"
#include "shadow_sampling.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
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
const glm::vec3 moonDirection = glm::normalize(glm::vec3(0.91f, 0.30f, 0.22f));
const glm::vec3 campSunDirection = glm::normalize(glm::vec3(0.83f, 0.40f, 0.27f));

constexpr const char* vertexShader = R"glsl(
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 instanceCenter;
layout (location = 3) in vec3 instanceSize;
layout (location = 4) in vec2 uv;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform bool uInstanced;

out vec3 worldPosition;
out vec3 worldNormal;
out vec2 textureUV;

void main() {
    vec3 local = uInstanced ? position * instanceSize + instanceCenter : position;
    vec4 world = uModel * vec4(local, 1.0);
    worldPosition = world.xyz;
    worldNormal = uInstanced ? normal / instanceSize : transpose(inverse(mat3(uModel))) * normal;
    textureUV = uv;
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

float skyHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float skyNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 blend = f * f * (3.0 - 2.0 * f);
    return mix(mix(skyHash(cell), skyHash(cell + vec2(1, 0)), blend.x),
               mix(skyHash(cell + vec2(0, 1)), skyHash(cell + vec2(1)), blend.x), blend.y);
}

float cloudCover(vec3 direction) {
    if (direction.y <= 0.015) return 0.0;
    vec2 p = direction.xz / (direction.y + 0.13) * vec2(1.8, 1.2);
    float clouds = skyNoise(p) * 0.57 + skyNoise(p * 2.1) * 0.28 + skyNoise(p * 4.3) * 0.15;
    return smoothstep(0.49, 0.72, clouds) * smoothstep(0.015, 0.13, direction.y);
}

vec3 cloudedSky(vec3 direction, float cover) {
    vec3 cloud = uNight ? vec3(0.026, 0.038, 0.061) : vec3(0.91, 0.93, 0.95);
    return mix(skyRadiance(direction), cloud, cover * 0.66);
}
)glsl";

constexpr const char* materialFragment = R"glsl(
in vec3 worldPosition;
in vec3 worldNormal;
in vec2 textureUV;

uniform vec3 uColor;
uniform vec3 uEye;
uniform vec4 uSurface;
uniform vec3 uEmissive;
uniform float uLensTint;
uniform bool uGroundPass;
uniform bool uMenuLight;
uniform int uCourse;
uniform vec3 uOvalDimensions;
uniform sampler2D uBaseColor;
uniform bool uTextured;
uniform vec2 uAlphaMask;
uniform bool uCampGround;
uniform bool uPaint;
uniform float uGroundHeight;
uniform bool uReceivesVehicleLights;
uniform bool uSceneReflections;
uniform sampler2D uReflectionScene;
uniform mat4 uViewProjection;
uniform float uReflectionLogRange;

out vec4 pixelColor;

float sunlight(vec3 normal) {
    if (uNight && uCourse != 2) {
        return 0.0;
    }
    if (uMenuLight) return 1.0;
    float angle = max(0.0, dot(normal, uSunDirection));
    float bias = uGroundPass ? mix(0.00026, 0.00007, angle) : mix(0.0015, 0.00042, angle);
    if (uHasDistantShadow) bias *= 0.53;
    return sceneShadow(worldPosition, bias, uHasDistantShadow ? 3 : 2);
}

float softbox(vec3 ray, vec3 axis, vec3 right, vec2 size) {
    float facing = dot(ray, axis);
    vec2 offset = abs(vec2(dot(ray, right), dot(ray, cross(axis, right))));
    vec2 edge = 1.0 - smoothstep(size * 0.70, size, offset / max(facing, 0.001));
    return edge.x * edge.y * step(0.0, facing);
}

vec3 paintEnvironment(vec3 reflection) {
    if (uMenuLight) {
        vec3 studio = mix(vec3(0.043, 0.055, 0.078), vec3(0.47, 0.60, 0.77),
                          smoothstep(-0.12, 0.42, reflection.y));
        float key = softbox(reflection, vec3(-0.60, 0.80, 0.0), vec3(0, 0, 1), vec2(1.28, 0.19));
        float fill = softbox(reflection, vec3(0.0, 0.60, 0.80), vec3(1, 0, 0), vec2(0.95, 0.16));
        return studio + key * vec3(3.6, 3.8, 4.0) + fill * vec3(1.75, 2.02, 2.38);
    }
    if (reflection.y > 0.0) return cloudedSky(reflection, cloudCover(reflection));
    float groundMix = smoothstep(0.0, 0.15, -reflection.y);
    vec3 ground = uCourse == 2 ? vec3(0.125, 0.158, 0.084) :
                  uCourse == 1 ? vec3(0.31, 0.22, 0.15) : vec3(0.10, 0.13, 0.16);
    if (uCourse == 0) {
        float distanceToGround = max(0.0, worldPosition.y - uGroundHeight) / max(0.015, -reflection.y);
        vec2 grid = (worldPosition.xz + reflection.xz * distanceToGround) / 2.5;
        vec2 distanceToLine = abs(fract(grid + 0.5) - 0.5);
        float line = (1.0 - smoothstep(0.005, 0.03, min(distanceToLine.x, distanceToLine.y))) *
                     (1.0 - smoothstep(3.0, 12.0, distanceToGround));
        ground += line * vec3(0.10, 0.13, 0.13);
    } else if (uCourse == 1) {
        float t = max(0.0, worldPosition.y - uGroundHeight) / max(0.02, -reflection.y);
        vec2 p = worldPosition.xz + reflection.xz * t - vec2(0.0, uOvalDimensions.y);
        vec2 turn = vec2(max(abs(p.x) - uOvalDimensions.x, 0.0), p.y);
        float edge = abs(length(turn) - uOvalDimensions.y);
        ground = mix(vec3(0.10, 0.12, 0.14), ground,
                     smoothstep(uOvalDimensions.z - 0.28, uOvalDimensions.z + 0.28, edge));
    }
    return mix(skyRadiance(reflection), ground * (uNight ? 0.16 : 1.0), groundMix);
}

vec4 sceneReflection(vec3 normal, vec3 direction) {
    vec3 start = worldPosition + normal * 0.035;
    float previousDistance = 0.0;
    for (int i = 1; i <= 13; ++i) {
        float stepIndex = float(i);
        float distance = stepIndex * (0.11 + stepIndex * 0.145);
        vec4 clip = uViewProjection * vec4(start + direction * distance, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = 0.5 + 0.5 * clip.xy / clip.w;
        float edge = min(min(uv.x, uv.y), 1.0 - max(uv.x, uv.y));
        if (edge < 0.0) break;
        vec4 samplePoint = textureLod(uReflectionScene, uv, 0.0);
        float sceneDepth = exp2(samplePoint.a * uReflectionLogRange) - 1.0;
        float delta = clip.w - sceneDepth;
        float thickness = 0.16 + (distance - previousDistance) * 0.86 + sceneDepth * 0.025;
        if (samplePoint.a < 0.998 && delta >= 0.0 && delta < thickness) {
            float fade = smoothstep(0.0, 0.10, edge) *
                         (1.0 - smoothstep(thickness * 0.54, thickness, delta)) *
                         (1.0 - smoothstep(12.0, 26.0, distance));
            return vec4(samplePoint.rgb, fade);
        }
        previousDistance = distance;
    }
    return vec4(0.0);
}

void main() {
    vec3 toEye = uEye - worldPosition;
    float distanceToEye = length(toEye);
    vec3 view = toEye / max(distanceToEye, 0.0001);
    float fog = uCourse == 2 ? 0.0 : 1.0 - exp(-distanceToEye * 0.014);
    if (uGroundPass) {
        vec2 grid = worldPosition.xz / 2.5;
        vec2 width = max(fwidth(grid), vec2(0.0001));
        vec2 lines = 1.0 - smoothstep(vec2(0.0), width, abs(fract(grid + 0.5) - 0.5));
        float line = max(lines.x, lines.y) * (1.0 - smoothstep(12.0, 55.0, distanceToEye));
        vec3 asphalt = mix(uColor, vec3(0.15, 0.19, 0.205), line * 0.68);
        if (uCourse == 1) {
            vec2 oval = worldPosition.xz - vec2(0.0, uOvalDimensions.y);
            vec2 turn = vec2(max(abs(oval.x) - uOvalDimensions.x, 0.0), oval.y);
            float distanceOnTrack = length(turn) - uOvalDimensions.y;
            float edge = abs(distanceOnTrack);
            float aa = max(fwidth(distanceOnTrack), 0.025);
            float curb = 1.0 - smoothstep(0.38, 0.38 + aa, abs(edge - 5.2));
            float paved = 1.0 - smoothstep(uOvalDimensions.z - 0.2 - aa, uOvalDimensions.z + 0.2 + aa, edge);
            float sand = 0.5 + 0.25 * sin(oval.x * 0.91 + oval.y * 0.35) * cos(oval.y * 0.83 - oval.x * 0.21);
            asphalt = mix(mix(vec3(0.41, 0.25, 0.15), vec3(0.52, 0.34, 0.21), sand),
                          vec3(0.083, 0.097, 0.116), paved);
            float distanceStripe = abs(oval.x) < uOvalDimensions.x ? oval.x : atan(turn.y, turn.x) * uOvalDimensions.y;
            asphalt = mix(asphalt, mix(vec3(0.87, 0.78, 0.60), vec3(0.50, 0.12, 0.08),
                          step(0.5, fract(distanceStripe * 0.56))), curb);
            float start = (1.0 - smoothstep(0.30, 0.30 + aa, abs(oval.x - 12.0))) * paved;
            float checker = mod(floor(oval.x * 5.0) + floor(oval.y * 3.0), 2.0);
            asphalt = mix(asphalt, mix(vec3(0.22), vec3(0.78), checker), start);
        }
        vec3 groundNormal = vec3(0, 1, 0);
        vec3 illumination = uNight ? vec3(0.16, 0.22, 0.30) : vec3(mix(0.44, 1.0, sunlight(groundNormal)));
        if (uReceivesVehicleLights) illumination += vehicleLighting(worldPosition, groundNormal);
        asphalt *= illumination;
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

    vec4 base = uTextured ? texture(uBaseColor, textureUV) : vec4(1.0);
    if (uAlphaMask.x >= 0.0 && base.a * uAlphaMask.y < uAlphaMask.x) discard;
    vec3 albedo = uColor * base.rgb;
    if (uCampGround) {
        vec2 p = worldPosition.xz * 0.006135923;
        float grain = sin(p.x * 181.0 + p.y * 139.0) * cos(p.x * 157.0 - p.y * 127.0);
        float patches = sin(p.x * 17.0 - p.y * 21.0) * sin(p.x * 11.0 + p.y * 15.0);
        albedo *= 1.0 + 0.055 * grain + 0.095 * patches;
    }
    float ambient = uNight ? (uCourse == 2 ? 0.23 : 0.06) : 0.22 + 0.31 * (normal.y * 0.5 + 0.5);
    float daylight = ambient + (uNight ? 0.38 : 0.62) * direct * max(dot(normal, uSunDirection), 0.0);
    vec3 diffuse = albedo * daylight * (1.0 - metallic * 0.86);
    if (uNight && uCourse == 2) diffuse *= vec3(0.71, 0.84, 1.04);
    vec3 specular = mix(mix(vec3(0.045), albedo, metallic), vec3(1.0), fresnel * 0.72);
    vec3 shaded = diffuse + environment * specular;
    float coatSun = pow(max(dot(reflection, uSunDirection), 0.0), 170.0);
    if (uPaint) {
        float coatFresnel = 0.04 + 0.96 * fresnel;
        vec3 coat = paintEnvironment(reflection);
        if (!uMenuLight) {
            if (uSceneReflections) {
                vec4 local = sceneReflection(normal, reflection);
                coat = mix(coat, local.rgb, local.a * 0.86);
            }
            float lightAngle = max(dot(normal, uSunDirection), 0.0);
            float sunShoulder = pow(max(dot(reflection, uSunDirection), 0.0), 36.0);
            vec3 sunColor = uNight ? vec3(0.26, 0.36, 0.60) : vec3(1.0, 0.87, 0.69);
            coat += direct * lightAngle * sunColor * (0.68 * sunShoulder + 11.0 * coatSun);
        }
        shaded = shaded * (1.0 - clearcoat * coatFresnel) + coat * clearcoat * coatFresnel;
    } else {
        shaded += skyRadiance(reflection) * clearcoat * (0.085 + 0.18 * fresnel);
        shaded += clearcoat * direct * coatSun * vec3(1.85, 1.55, 1.18);
    }
    if (uReceivesVehicleLights) shaded += albedo * vehicleLighting(worldPosition, normal);
    shaded += uEmissive;
    pixelColor = vec4(mix(shaded, skyRadiance(-view), fog), 1.0);
}
)glsl";

constexpr const char* skyFragment = R"glsl(
in vec2 clipPosition;
uniform mat4 uClipToWorld;
uniform bool uMenu;
uniform bool uCamp;
uniform float uTime;
uniform float uMenuColor;
out vec4 pixelColor;

void main() {
    if (uMenu) {
        vec2 p = clipPosition * 0.5 + 0.5;
        float wave = sin(p.x * 5.2 - uTime * 0.32) * 0.060 + sin(p.x * 2.5 + uTime * 0.19) * 0.075;
        float sweep = p.y + p.x * 0.48 + wave;
        float dither = (skyHash(mod(gl_FragCoord.xy, 128.0)) - 0.5) * 0.026;
        float blue = smoothstep(0.75, 0.88, sweep + dither);
        vec3 tone = mix(vec3(0.83, 0.91, 0.96), vec3(0.25, 0.52, 0.71), blue);
        float ribbon = 1.0 - smoothstep(0.009, 0.014, abs(sweep - 0.755));
        float inner = 1.0 - smoothstep(0.024, 0.028, abs(sweep - 0.865));
        tone = mix(tone, vec3(0.40, 0.75, 0.91), ribbon * 0.72);
        tone = mix(tone, vec3(0.42, 0.68, 0.84), inner * 0.49);
        vec2 cell = fract(gl_FragCoord.xy / 6.0) - 0.5;
        float dotField = (1.0 - smoothstep(0.13, 0.34, length(cell))) *
                         smoothstep(0.69, 0.80, sweep) * (1.0 - smoothstep(0.92, 1.09, sweep));
        tone *= 1.0 - dotField * 0.20 + dither * 0.75;
        float halo = 1.0 - smoothstep(0.02, 0.54, length((p - vec2(0.77, 0.51)) * vec2(0.8, 1.0)));
        tone += halo * vec3(0.086, 0.10, 0.12);
        float speed = fract(p.x * 0.75 + p.y * 5.0 + uTime * 0.15);
        tone += vec3(0.055, 0.095, 0.12) * (1.0 - smoothstep(0.0, 0.021, abs(speed - 0.47))) * blue;
        float road = p.y + 0.24 * p.x + 0.035 * sin(5.0 * p.x + uTime * 0.27);
        float thinStripe = 1.0 - smoothstep(0.002, 0.004, abs(road - 0.349));
        float broadStripe = 1.0 - smoothstep(0.016, 0.019, abs(road - 0.315));
        tone = mix(tone, vec3(0.47, 0.75, 0.90), (thinStripe * 0.30 + broadStripe * 0.36) * smoothstep(0.45, 0.96, p.x));
        float paper = (1.0 - smoothstep(0.05, 0.55, p.x)) * 0.41;
        tone = mix(tone, vec3(0.87, 0.94, 0.98), paper);
        pixelColor = vec4(mix(vec3(1.0), tone, uMenuColor), 1.0);
        return;
    }
    vec3 ray = normalize((uClipToWorld * vec4(clipPosition, 1.0, 1.0)).xyz);
    float cover = cloudCover(ray);
    vec3 sky = cloudedSky(ray, cover);
    if (ray.y > 0.0) {
        float sun = smoothstep(0.99962, 0.9998, dot(ray, uSunDirection));
        if (!uNight) {
            sky += vec3(4.8, 3.9, 2.5) * sun;
        } else if (uCamp) {
            float moon = smoothstep(0.99986, 0.99991, dot(ray, uSunDirection));
            sky += vec3(1.5, 1.72, 2.1) * moon * (1.0 - cover * 0.63);
        }
    }
    pixelColor = vec4(sky, 1.0);
}
)glsl";

void compileShader(GLuint shader, const char* source)
{
#ifdef __EMSCRIPTEN__
    constexpr const char* header = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
#else
    constexpr const char* header = "#version 330 core\n";
#endif
    const char* sources[]{header, source};
    glShaderSource(shader, 2, sources, nullptr);
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

MeshPart makeCampProp(CampProp kind)
{
    constexpr std::array<glm::vec3, 5> colors{{
        {0.12f, 0.43f, 0.19f}, {0.24f, 0.145f, 0.092f}, {0.34f, 0.27f, 0.20f},
        {0.35f, 0.245f, 0.145f}, {0.39f, 0.47f, 0.39f},
    }};
    constexpr std::array<glm::vec3, 8> cube{{
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f},
        {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
    }};
    MeshPart mesh;
    mesh.surface.color = colors[static_cast<std::size_t>(kind)];
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
        const auto normal = glm::normalize(glm::cross(b - a, c - a));
        mesh.vertices.insert(mesh.vertices.end(), {{a, normal}, {b, normal}, {c, normal}});
        mesh.indices.insert(mesh.indices.end(), {index, index + 1, index + 2});
    };
    const auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        triangle(a, b, c);
        triangle(a, c, d);
    };
    if (kind == CampProp::Canvas) {
        constexpr glm::vec3 left{-0.5f, 0.5f, 0.0f}, right{0.5f, 0.5f, 0.0f};
        triangle(cube[0], cube[3], left);
        triangle(cube[2], cube[1], right);
        face(cube[1], cube[0], left, right);
        face(cube[3], cube[2], right, left);
    } else {
        face(cube[0], cube[1], cube[2], cube[3]);
        face(cube[7], cube[6], cube[5], cube[4]);
        face(cube[1], cube[0], cube[4], cube[5]);
        face(cube[3], cube[2], cube[6], cube[7]);
        face(cube[0], cube[3], cube[7], cube[4]);
        face(cube[2], cube[1], cube[5], cube[6]);
    }
    return mesh;
}

} // namespace

void ChaseCamera::orbit(const glm::vec2& delta)
{
    yaw = std::remainder(yaw - delta.x * 0.006f, 2.0f * pi);
    pitch = std::clamp(pitch + delta.y * 0.0045f, -0.29f, 0.68f);
}

void ChaseCamera::update(const glm::vec2& mouseDelta, bool looking, float dt)
{
    if (looking) {
        orbit(mouseDelta);
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
      mirrorAcrossBody_(part.mirrorAcrossBody), carPart_(part.carPart),
      alphaCutoff_(part.alphaCutoff), alphaFactor_(part.alphaFactor)
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
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);
    if (part.baseColor) {
        const auto& image = *part.baseColor;
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

Renderer::MeshBuffer::MeshBuffer(MeshBuffer&& other) noexcept
    : vertexArray_(std::exchange(other.vertexArray_, 0)),
      vertexBuffer_(std::exchange(other.vertexBuffer_, 0)),
      indexBuffer_(std::exchange(other.indexBuffer_, 0)),
      texture_(std::exchange(other.texture_, 0)), indexCount_(other.indexCount_), surface_(other.surface_),
      mirrorAcrossBody_(other.mirrorAcrossBody_), carPart_(other.carPart_),
      alphaCutoff_(other.alphaCutoff_), alphaFactor_(other.alphaFactor_)
{
}

Renderer::MeshBuffer::~MeshBuffer()
{
    glDeleteVertexArrays(1, &vertexArray_);
    glDeleteBuffers(1, &vertexBuffer_);
    glDeleteBuffers(1, &indexBuffer_);
    glDeleteTextures(1, &texture_);
}

void Renderer::MeshBuffer::draw() const
{
    glBindVertexArray(vertexArray_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void Renderer::MeshBuffer::drawInstances(GLuint buffer, GLsizei count) const
{
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexAttribArray(i + 2);
        glVertexAttribPointer(i + 2, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec3),
                              reinterpret_cast<const void*>(i * sizeof(glm::vec3)));
        glVertexAttribDivisor(i + 2, 1);
    }
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, count);
}

class Renderer::CampRender {
public:
    CampRender(const std::filesystem::path& assets, const SceneryQuality& quality)
        : quality_(quality), grass_(quality_)
    {
        if (quality_.distantShadowExtent > 0.0f) {
            distantShadow_ = std::make_unique<SunShadow>(quality_.shadowResolution);
        }
        meshes_.reserve(propCount);
        for (std::size_t i = 0; i < propCount; ++i) {
            meshes_.emplace_back(makeCampProp(static_cast<CampProp>(i)));
        }
        for (std::size_t lod = quality_.nearestTreeLod; lod < lodMeshes_.size(); ++lod) {
            const auto path = assets / ("camp-tree-lod" + std::to_string(lod) + ".glb");
            if (!std::filesystem::is_regular_file(path)) continue;
            try {
                const auto parts = loadScenery(path);
                if (treeHeight_ == 0.0f) {
                    float low = std::numeric_limits<float>::max(), high = -low;
                    for (const auto& part : parts) {
                        for (const auto& vertex : part.vertices) {
                            low = std::min(low, vertex.position.y);
                            high = std::max(high, vertex.position.y);
                        }
                    }
                    if (high - low < 0.01f) throw std::runtime_error("Tree mesh needs a positive height");
                    treeBase_ = low;
                    treeHeight_ = high - low;
                }
                for (const auto& part : parts) lodMeshes_[lod].emplace_back(part);
            } catch (const std::runtime_error& error) {
                std::cerr << path.filename().string() << ": " << error.what() << '\n';
            }
        }
        glGenBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
    }

    ~CampRender() { glDeleteBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data()); }

    void update(const CampWorld& world, const glm::vec3& position, const glm::vec3& eye, float screenScale)
    {
        if (seed_ != world.seed()) {
            seed_ = world.seed();
            chunks_.clear();
        }
        const auto center = world.tileAt(position);
        const bool extended = quality_.viewDistance > 530.0f;
        const double streamRadius = quality_.viewDistance / campTerrainTileSize + 1.5;
        const auto span = static_cast<std::int64_t>(std::ceil(streamRadius));
        const std::int64_t along = extended ? span : 3;
        const std::int64_t across = extended ? span : 2;
        const auto distance = [&](const CampTile& tile) {
            const auto dx = tile[0] - center[0], dz = tile[1] - center[1];
            return dx * dx + dz * dz;
        };
        const auto visible = [&](const CampTile& tile) {
            return extended ? distance(tile) <= streamRadius * streamRadius
                : std::abs(tile[0] - center[0]) <= along && std::abs(tile[1] - center[1]) <= across;
        };
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            if (!visible(it->first)) it = chunks_.erase(it);
            else ++it;
        }
        struct Request { CampTile key; int stride; };
        std::vector<Request> missing;
        for (auto x = center[0] - along; x <= center[0] + along; ++x) {
            for (auto z = center[1] - across; z <= center[1] + across; ++z) {
                const CampTile key{x, z};
                if (!visible(key)) continue;
                const auto found = chunks_.find(key);
                const auto dist = distance(key);
                const int stride = !extended || dist <= 13 ? 1 : (dist <= 43 ? 2 : 4);
                if (found == chunks_.end() || found->second.stride > stride) missing.push_back({key, stride});
            }
        }
        std::sort(missing.begin(), missing.end(), [&](const Request& a, const Request& b) {
            return distance(a.key) < distance(b.key);
        });
        const bool nearWork = !missing.empty() && missing.front().stride == 1;
        const std::size_t budget = chunks_.empty() ? 9 : (extended && !nearWork ? 24 : 4);
        for (std::size_t i = 0; i < std::min(budget, missing.size()); ++i) {
            const auto& request = missing[i];
            CampChunk data = world.buildChunk(request.key, request.stride);
            glm::vec3 low(std::numeric_limits<float>::max()), high = -low;
            for (const auto& surface : data.surfaces) {
                for (const auto& vertex : surface.vertices) {
                    low = glm::min(low, vertex.position);
                    high = glm::max(high, vertex.position);
                }
            }
            Chunk chunk{data.origin, request.stride, (low + high) * 0.5f, (high - low) * 0.5f, {}, std::move(data.props)};
            for (const auto& surface : data.surfaces) {
                if (!surface.indices.empty()) chunk.surfaces.emplace_back(surface);
            }
            chunks_.insert_or_assign(request.key, std::move(chunk));
        }

        for (auto& props : instances_) props.clear();
        for (const auto& entry : chunks_) {
            const Chunk& chunk = entry.second;
            const glm::vec3 origin = world.localOrigin(chunk.origin);
            const auto separation = glm::max(glm::abs(origin + chunk.boundsCenter - eye) - chunk.boundsHalf - 9.0f, 0.0f);
            if (glm::dot(separation, separation) > quality_.propDistance * quality_.propDistance) continue;
            for (const auto& prop : chunk.props) {
                glm::vec3 centerPoint = origin + prop.center;
                glm::vec3 size = prop.size;
                const float distanceToEye = std::max(glm::length(centerPoint - eye), 1.0f);
                if (distanceToEye > quality_.propDistance) continue;
                const float apparentHeight = size.y * screenScale / distanceToEye;
                if (prop.kind == CampProp::Tree) {
                    if (apparentHeight < quality_.minTreePixels) continue;
                    const bool near = quality_.nearestTreeLod == 0 && apparentHeight > quality_.nearTreePixels;
                    std::size_t lod = near ? 0 : apparentHeight > quality_.midTreePixels ? 1 : 2;
                    while (lod < lodMeshes_.size() && lodMeshes_[lod].empty()) ++lod;
                    if (lod < lodMeshes_.size()) {
                        const float scale = size.y / treeHeight_;
                        centerPoint.y -= size.y * 0.5f + treeBase_ * scale;
                        instances_[propCount + lod].push_back({centerPoint, glm::vec3(scale)});
                        continue;
                    }
                    const float trunk = size.y * 0.28f;
                    if (apparentHeight > (quality_.nearestTreeLod == 0 ? 85.0f : 175.0f)) {
                        instances_[static_cast<std::size_t>(CampProp::Timber)].push_back({
                            centerPoint - up * (size.y - trunk) * 0.5f, {size.x * 0.12f, trunk, size.z * 0.12f}});
                        centerPoint.y += trunk * 0.5f;
                        size.y -= trunk;
                    }
                } else if (apparentHeight < 2.0f) {
                    continue;
                }
                instances_[static_cast<std::size_t>(prop.kind)].push_back({centerPoint, size});
            }
        }
        for (std::size_t i = 0; i < instances_.size(); ++i) {
            auto& batch = instances_[i];
            if (batch.empty()) {
                localShadowCounts_[i] = distantShadowCounts_[i] = 0;
                continue;
            }
            const auto within = [&](const Instance& instance, float extent) {
                const glm::vec3 offset = instance.center - position;
                const float radius = extent * 2.45f + glm::length(instance.size) * 0.5f;
                return glm::dot(offset, offset) < radius * radius;
            };
            const auto localEnd = std::partition(batch.begin(), batch.end(), [&](const Instance& instance) {
                return within(instance, quality_.shadowExtent);
            });
            localShadowCounts_[i] = static_cast<GLsizei>(localEnd - batch.begin());
            if (distantShadow_) {
                const auto distantEnd = std::partition(localEnd, batch.end(), [&](const Instance& instance) {
                    return within(instance, quality_.distantShadowExtent);
                });
                distantShadowCounts_[i] = static_cast<GLsizei>(distantEnd - batch.begin());
            }
            glBindBuffer(GL_ARRAY_BUFFER, buffers_[i]);
            glBufferData(GL_ARRAY_BUFFER, batch.size() * sizeof(Instance), batch.data(), GL_STREAM_DRAW);
        }
        grass_.update(world, position);
    }

    const SceneryQuality& quality() const { return quality_; }
    const GrassRenderer& grass() const { return grass_; }
    SunShadow* distantShadow() const { return distantShadow_.get(); }

    template<class Draw>
    void drawSurfaces(const CampWorld& world, const glm::mat4& clip, Draw draw) const
    {
        const auto rows = glm::transpose(clip);
        const std::array<glm::vec4, 6> planes{rows[3] + rows[0], rows[3] - rows[0],
            rows[3] + rows[1], rows[3] - rows[1], rows[3] + rows[2], rows[3] - rows[2]};
        for (const auto& entry : chunks_) {
            const Chunk& chunk = entry.second;
            const auto origin = world.localOrigin(chunk.origin);
            const auto center = glm::vec4(origin + chunk.boundsCenter, 1.0f);
            if (std::any_of(planes.begin(), planes.end(), [&](const auto& plane) {
                return glm::dot(plane, center) + glm::dot(glm::abs(glm::vec3(plane)), chunk.boundsHalf) < 0.0f;
            })) continue;
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), origin);
            for (const auto& surface : chunk.surfaces) draw(surface, model);
        }
    }

    template<class Draw>
    void drawProps(Draw draw, const std::array<GLsizei, static_cast<std::size_t>(CampProp::Count) + 3>* counts = nullptr) const
    {
        const auto countFor = [&](std::size_t i) {
            return counts ? (*counts)[i] : static_cast<GLsizei>(instances_[i].size());
        };
        for (std::size_t i = 0; i < propCount; ++i) {
            const auto count = countFor(i);
            if (count > 0) draw(meshes_[i], buffers_[i], count);
        }
        for (std::size_t lod = 0; lod < lodMeshes_.size(); ++lod) {
            const auto batch = propCount + lod;
            const auto count = countFor(batch);
            if (count == 0) continue;
            for (const auto& mesh : lodMeshes_[lod]) {
                draw(mesh, buffers_[batch], count);
            }
        }
    }

    template<class Draw>
    void drawShadowProps(bool distant, Draw draw) const
    {
        drawProps(draw, distant ? &distantShadowCounts_ : &localShadowCounts_);
    }

private:
    struct Instance { glm::vec3 center; glm::vec3 size; };
    struct Chunk {
        glm::dvec2 origin;
        int stride;
        glm::vec3 boundsCenter;
        glm::vec3 boundsHalf;
        std::vector<MeshBuffer> surfaces;
        std::vector<CampPlacement> props;
    };
    static constexpr auto propCount = static_cast<std::size_t>(CampProp::Count);
    const SceneryQuality quality_;
    GrassRenderer grass_;
    std::unique_ptr<SunShadow> distantShadow_;
    std::uint32_t seed_ = 0;
    float treeBase_ = 0.0f;
    float treeHeight_ = 0.0f;
    std::map<CampTile, Chunk> chunks_;
    std::vector<MeshBuffer> meshes_;
    std::array<std::vector<MeshBuffer>, 3> lodMeshes_;
    std::array<std::vector<Instance>, propCount + 3> instances_;
    std::array<GLsizei, propCount + 3> localShadowCounts_{};
    std::array<GLsizei, propCount + 3> distantShadowCounts_{};
    std::array<GLuint, propCount + 3> buffers_{};
};

Renderer::Renderer(const CarAsset& asset, const std::filesystem::path& assets)
    : ground_(makeGround(asset.groundHeight)), assets_(assets), quality_(sceneryQuality()),
      shadow_(std::make_unique<SunShadow>(quality_.shadowResolution))
{
    body_.reserve(asset.body.size());
    for (const MeshPart& part : asset.body) {
        body_.emplace_back(part);
    }
    wheel_.reserve(asset.wheel.size() + asset.brakeDisc.size());
    for (const MeshPart& part : asset.wheel) {
        wheel_.emplace_back(part);
    }
    for (const MeshPart& part : asset.brakeDisc) {
        wheel_.emplace_back(part);
    }
    caliper_.reserve(asset.brakeCaliper.size());
    for (const MeshPart& part : asset.brakeCaliper) {
        caliper_.emplace_back(part);
    }

    const std::string materialCode = std::string(skyLighting) + shadowSamplingGlsl + vehicleLightingGlsl + materialFragment;
    const std::string skyCode = std::string(skyLighting) + skyFragment;
    program_ = createShaderProgram(vertexShader, materialCode.c_str());
    try {
        skyProgram_ = createShaderProgram(fullscreenVertex, skyCode.c_str());
    } catch (...) {
        glDeleteProgram(program_);
        throw;
    }
    glGenVertexArrays(1, &skyArray_);
    glGenTextures(1, &whiteTexture_);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    constexpr std::array<unsigned char, 4> white{255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uBaseColor"), 4);
    glUniform1i(glGetUniformLocation(program_, "uReflectionScene"), 6);
    skyClipToWorld_ = glGetUniformLocation(skyProgram_, "uClipToWorld");
    skyNight_ = glGetUniformLocation(skyProgram_, "uNight");
    skyCamp_ = glGetUniformLocation(skyProgram_, "uCamp");
    skySunDirection_ = glGetUniformLocation(skyProgram_, "uSunDirection");
    model_ = glGetUniformLocation(program_, "uModel");
    instanced_ = glGetUniformLocation(program_, "uInstanced");
    textured_ = glGetUniformLocation(program_, "uTextured");
    alphaMask_ = glGetUniformLocation(program_, "uAlphaMask");
    campGround_ = glGetUniformLocation(program_, "uCampGround");
    viewProjection_ = glGetUniformLocation(program_, "uViewProjection");
    color_ = glGetUniformLocation(program_, "uColor");
    paint_ = glGetUniformLocation(program_, "uPaint");
    receivesVehicleLights_ = glGetUniformLocation(program_, "uReceivesVehicleLights");
    sceneReflections_ = glGetUniformLocation(program_, "uSceneReflections");
    reflectionLogRange_ = glGetUniformLocation(program_, "uReflectionLogRange");
    vehicleLights_.initialize(program_);
    groundHeight_ = glGetUniformLocation(program_, "uGroundHeight");
    surface_ = glGetUniformLocation(program_, "uSurface");
    emissive_ = glGetUniformLocation(program_, "uEmissive");
    lensTint_ = glGetUniformLocation(program_, "uLensTint");
    eye_ = glGetUniformLocation(program_, "uEye");
    groundPass_ = glGetUniformLocation(program_, "uGroundPass");
    course_ = glGetUniformLocation(program_, "uCourse");
    ovalDimensions_ = glGetUniformLocation(program_, "uOvalDimensions");
    menuLight_ = glGetUniformLocation(program_, "uMenuLight");
    night_ = glGetUniformLocation(program_, "uNight");
    sunDirection_ = glGetUniformLocation(program_, "uSunDirection");
    shadows_.initialize(program_, 0);
    menuSky_ = glGetUniformLocation(skyProgram_, "uMenu");
    menuTime_ = glGetUniformLocation(skyProgram_, "uTime");
    menuColor_ = glGetUniformLocation(skyProgram_, "uMenuColor");
    glEnable(GL_DEPTH_TEST);
}

Renderer::~Renderer()
{
    glDeleteVertexArrays(1, &skyArray_);
    glDeleteTextures(1, &whiteTexture_);
    glDeleteProgram(skyProgram_);
    glDeleteProgram(program_);
}

void Renderer::draw(const MeshBuffer& mesh, const glm::mat4& model, bool night, bool braking,
                    GLuint instances, GLsizei count, const glm::vec3* paint) const
{
    glUniform1i(instanced_, instances != 0);
    glUniform1i(textured_, mesh.texture() != 0);
    glUniform2f(alphaMask_, mesh.alphaCutoff(), mesh.alphaFactor());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, mesh.texture() ? mesh.texture() : whiteTexture_);
    glActiveTexture(GL_TEXTURE0);
    glUniformMatrix4fv(model_, 1, GL_FALSE, glm::value_ptr(model));
    const Surface& surface = mesh.surface();
    const bool headlamp = surface.lightControl == LightControl::Headlamp;
    const bool emitting = headlamp || surface.lightControl == LightControl::Always ||
                          (surface.lightControl == LightControl::Night && night) ||
                          (surface.lightControl == LightControl::Brake && braking);
    const glm::vec3 emission = emitting ? surface.emission * (headlamp && night ? 1.65f : 1.0f) : glm::vec3(0.0f);
    glUniform3fv(color_, 1, glm::value_ptr(surface.paint && paint ? *paint : surface.color));
    glUniform1i(paint_, surface.paint);
    glUniform3fv(emissive_, 1, glm::value_ptr(emission));
    glUniform1f(lensTint_, surface.lensTint);
    glUniform4f(surface_, surface.metallic, surface.roughness, surface.clearcoat, surface.opacity);
    if (instances != 0) mesh.drawInstances(instances, count);
    else mesh.draw();
}

void Renderer::setGraphics(const GraphicsSettings& graphics)
{
    if (graphics.preset != graphics_.preset || graphics.lod != graphics_.lod) {
        const auto quality = sceneryQuality(graphics.preset, graphics.lod);
        camp_.reset();
        if (quality.shadowResolution != quality_.shadowResolution) {
            shadow_ = std::make_unique<SunShadow>(quality.shadowResolution);
        }
        quality_ = quality;
    }
    graphics_ = graphics;
}

void Renderer::render(const CarAsset& asset, const CarState& car, const ChaseCamera& camera,
                      TireEffects& effects, int width, int height, bool night, const SceneView& scene)
{
    const glm::mat4 identity(1.0f);
    const CampWorld* camp = scene.menu ? nullptr : scene.camp;
    const glm::vec3 lightDirection = camp ? (night ? moonDirection : campSunDirection) : sunDirection;
    const glm::vec3 direction = forward(car.heading);
    const glm::vec3 orbit = forward(car.heading + camera.yaw);
    const float pitch = 0.35f + camera.pitch;
    const float cameraDistance = scene.titleDrift ? 5.65f : (scene.menu ? 6.1f : 7.2f);
    const glm::vec3 focus = car.position + up * (0.45f + car.suspension.heave * 0.30f);
    const glm::vec3 cameraSide = glm::cross(orbit, up);
    const float phase = camera.vibrationPhase;
    const float sideways = 0.026f * std::sin(7.0f * phase) + 0.012f * std::sin(11.0f * phase);
    const float vertical = 0.019f * std::sin(9.0f * phase + 0.6f) + 0.008f * std::sin(13.0f * phase);
    const glm::vec3 vibration = (cameraSide * sideways + up * vertical) * camera.speedEffect;
    glm::vec3 eye = focus - orbit * (cameraDistance * std::cos(pitch)) +
                   up * (cameraDistance * std::sin(pitch)) + vibration;
    if (camp) eye.y = std::max(eye.y, camp->surface(eye).height + 0.62f);
    const glm::vec3 target = focus + direction * 0.45f + vibration * 0.25f;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 view = glm::lookAt(eye, target, up);
    const float fov = 56.0f + 5.5f * camera.speedEffect;
    constexpr float menuAspect = 1280.0f / 720.0f;
    if (camp) {
        if (!camp_) camp_ = std::make_unique<CampRender>(assets_, quality_);
    } else if (scene.course != Course::DerelictCamp) {
        camp_.reset();
    }
    const float viewDistance = camp ? camp_->quality().viewDistance : 530.0f;
    const float nearPlane = viewDistance > 530.0f ? 0.14f : 0.05f;
    glm::mat4 projection = glm::perspective(glm::radians(fov), scene.menu ? menuAspect : aspect, nearPlane, viewDistance);
    if (scene.menu) {
        const float widthFit = std::min(1.0f, menuAspect / aspect);
        const float heightFit = std::min(1.0f, aspect / menuAspect);
        projection[0][0] *= widthFit;
        projection[1][1] *= heightFit;
        projection[2][0] = -0.43f * widthFit;
        projection[2][1] = (scene.titleDrift ? 0.10f : -0.02f) * heightFit;
    }
    const glm::mat4 viewProjection = projection * view;
    if (camp) {
        camp_->update(*camp, car.position, eye, projection[1][1] * height * graphics_.resolutionScale() * 0.5f);
    }

    const glm::mat4 baseModel = vehicleTransform(asset, car, camp != nullptr);
    const glm::mat4 bodyModel = bodyTransform(asset, car, baseModel);
    const bool castLights = !scene.menu && (night || car.brakeLights);
    VehicleLights lamps;
    if (castLights) {
        lamps.direction = glm::normalize(glm::mat3(bodyModel) * glm::vec3(1.0f, -0.045f, 0.0f));
        for (std::size_t side = 0; side < asset.headlights.size(); ++side) {
            lamps.lamps[side] = {glm::vec3(bodyModel * glm::vec4(asset.headlights[side], 1.0f)), night ? 1.0f : 0.0f};
            lamps.lamps[side + 2] = {glm::vec3(bodyModel * glm::vec4(asset.taillights[side], 1.0f)),
                                   (night ? 0.30f : 0.0f) + (car.brakeLights ? (night ? 0.82f : 0.28f) : 0.0f)};
        }
    }
    const glm::mat4 mirrored = glm::scale(bodyModel, glm::vec3(1.0f, 1.0f, -1.0f));
    std::array<glm::mat4, 4> wheelModels;
    std::array<glm::mat4, 4> caliperModels;
    const glm::mat4 mirrorAxle = glm::scale(identity, glm::vec3(1.0f, 1.0f, -1.0f));
    for (std::size_t i = 0; i < asset.hubs.size(); ++i) {
        glm::vec3 groundHub = asset.hubs[i];
        groundHub.y = asset.groundHeight + asset.wheelRadius;
        if (camp) groundHub.y += car.wheels[i].groundTravel;
        const glm::mat4 hub = glm::translate(baseModel, groundHub);
        const glm::mat4 steering = i < 2 ? glm::rotate(hub, frontWheelSteering(asset, i, car.steering), up) : hub;
        const glm::mat4 roll = glm::rotate(identity, car.wheels[i].angle, axle);
        const glm::mat4 facing = i % 2 == 1 ? glm::rotate(identity, pi, up) : identity;
        wheelModels[i] = steering * roll * facing * asset.wheelTransform;
        // Calipers follow suspension and steering. Mirroring keeps them on the same side of the axle.
        caliperModels[i] = steering * (i % 2 == 1 ? mirrorAxle : identity) * asset.wheelTransform;
    }
    const auto drawOpaque = [&](auto&& drawPart) {
        for (const MeshBuffer& part : body_) {
            if (part.surface().opacity >= 1.0f && car.setup.shows(part.carPart())) {
                drawPart(part, bodyModel);
                if (part.mirrorAcrossBody()) {
                    drawPart(part, mirrored);
                }
            }
        }
        for (std::size_t i = 0; i < wheelModels.size(); ++i) {
            for (const MeshBuffer& part : wheel_) {
                drawPart(part, wheelModels[i]);
            }
            for (const MeshBuffer& part : caliper_) {
                drawPart(part, caliperModels[i]);
            }
        }
    };
    SceneShadows lighting;
    lighting.nearDepth = shadow_->depthTexture();
    if ((!night || camp) && !scene.menu) {
        const auto cast = [&](SunShadow& map, float extent, bool distant) {
            const auto matrix = map.begin(car.position, lightDirection, extent);
            const auto drawDepth = [&](const MeshBuffer& part, const glm::mat4& model) {
                map.setModel(model);
                map.setMask(part.texture() ? part.texture() : whiteTexture_, part.alphaCutoff(), part.alphaFactor());
                part.draw();
            };
            drawOpaque(drawDepth);
            if (camp) {
                camp_->drawSurfaces(*camp, matrix, drawDepth);
                map.setModel(identity);
                map.setInstanced(true);
                camp_->drawShadowProps(distant, [&](const MeshBuffer& prop, GLuint buffer, GLsizei count) {
                    map.setMask(prop.texture() ? prop.texture() : whiteTexture_, prop.alphaCutoff(), prop.alphaFactor());
                    prop.drawInstances(buffer, count);
                });
            }
            map.end();
            return matrix;
        };
        lighting.nearMatrix = cast(*shadow_, camp ? camp_->quality().shadowExtent : 12.0f, false);
        if (camp) {
            if (auto* distant = camp_->distantShadow()) {
                lighting.distantMatrix = cast(*distant, camp_->quality().distantShadowExtent, true);
                lighting.distantDepth = distant->depthTexture();
            }
        }
    }

    post_.begin(width, height, graphics_.resolutionScale());
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(skyProgram_);
    const glm::mat4 clipToSky = glm::inverse(projection * glm::mat4(glm::mat3(view)));
    glUniformMatrix4fv(skyClipToWorld_, 1, GL_FALSE, glm::value_ptr(clipToSky));
    glUniform1i(skyNight_, night ? GL_TRUE : GL_FALSE);
    glUniform1i(skyCamp_, camp != nullptr);
    glUniform1i(menuSky_, scene.menu ? GL_TRUE : GL_FALSE);
    glUniform1f(menuTime_, scene.time);
    glUniform1f(menuColor_, scene.menuColor);
    glUniform3fv(skySunDirection_, 1, glm::value_ptr(lightDirection));
    glBindVertexArray(skyArray_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    if (scene.menu && !scene.showCar) {
        post_.present(glm::vec3(0.0f), 0.0f, scene.ripple, scene.menu, graphics_.bloom);
        return;
    }

    glUseProgram(program_);
    shadows_.bind(lighting);
    vehicleLights_.bind(lamps);
    glUniform1i(receivesVehicleLights_, castLights);
    glUniform1i(sceneReflections_, GL_FALSE);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(night_, night ? GL_TRUE : GL_FALSE);
    glUniform1i(menuLight_, scene.menu ? GL_TRUE : GL_FALSE);
    glUniform1i(campGround_, GL_FALSE);
    const Course shaderCourse = scene.menu ? Course::TestGrid : scene.course;
    glUniform1i(course_, static_cast<int>(shaderCourse));
    glUniform3f(ovalDimensions_, ovalStraightHalfLength, ovalTurnRadius, ovalRoadHalfWidth);
    glUniform1f(groundHeight_, asset.groundHeight);
    glUniform3fv(sunDirection_, 1, glm::value_ptr(lightDirection));
    glUniform3fv(eye_, 1, glm::value_ptr(eye));
    glUniformMatrix4fv(viewProjection_, 1, GL_FALSE, glm::value_ptr(viewProjection));

    if (camp) {
        glUniform1i(groundPass_, GL_FALSE);
        glUniform1i(campGround_, GL_TRUE);
        camp_->drawSurfaces(*camp, viewProjection, [&](const MeshBuffer& part, const glm::mat4& model) {
            draw(part, model, night, false);
        });
        effects.renderMarks(viewProjection, 0, true);
        glUseProgram(program_);
        glUniform1i(campGround_, GL_FALSE);
        camp_->drawProps([&](const MeshBuffer& prop, GLuint buffer, GLsizei count) {
            draw(prop, identity, night, false, buffer, count);
        });
        const auto& outlets = car.setup.diesel ? asset.dieselExhaust : asset.gasolineExhaust;
        const float rpmFraction = (car.rpm - idleRpm) / (powertrainSpec(asset.model, car.setup.diesel).torqueCurve.back().rpm - idleRpm);
        const float exhaustFlow = car.engineLoad * glm::smoothstep(0.08f, 0.8f, rpmFraction);
        camp_->grass().draw(viewProjection, lighting, lamps, camp->origin(), scene.time, night, outlets, bodyModel, exhaustFlow);
        glUseProgram(program_);
    } else if (!scene.menu) {
        glUniform1i(groundPass_, GL_TRUE);
        draw(ground_, identity, night, false);
        effects.renderMarks(viewProjection);
        glUseProgram(program_);
    } else if (scene.titleDrift) {
        effects.renderContactShadow(asset, car, viewProjection);
        effects.renderMarks(viewProjection, 100);
        glUseProgram(program_);
    }
    glUniform1i(groundPass_, GL_FALSE);
    glUniform1i(receivesVehicleLights_, GL_FALSE);

    if (!scene.menu && graphics_.reflections) {
        const float logRange = std::log2(1.0f + viewDistance);
        const auto texture = post_.captureReflections(projection, logRange);
        glUseProgram(program_);
        shadows_.bind(lighting);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(sceneReflections_, GL_TRUE);
        glUniform1f(reflectionLogRange_, logRange);
    }

    const glm::vec3 srgb = glm::vec3(glm::max(car.setup.paint, glm::ivec3(0))) / 255.0f;
    const glm::vec3 linearPaint = glm::mix(srgb / 12.92f, glm::pow((srgb + 0.055f) / 1.055f, glm::vec3(2.4f)),
                                          glm::greaterThan(srgb, glm::vec3(0.04045f)));
    const glm::vec3* paint = car.setup.paint.x >= 0 ? &linearPaint : nullptr;
    drawOpaque([&](const MeshBuffer& part, const glm::mat4& model) {
        draw(part, model, night, car.brakeLights, 0, 0, paint);
    });

    const auto drawGlass = [&]() {
        for (const MeshBuffer& part : body_) {
            if (part.surface().opacity < 1.0f && car.setup.shows(part.carPart())) {
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

    if (!scene.menu || scene.titleDrift) effects.renderSmoke(viewProjection, view, eye, night);
    if (camp) {
        const auto& quality = camp_->quality();
        post_.renderCampFog(glm::inverse(viewProjection), eye, lighting, lightDirection, camp->origin(), car.position.y,
                            scene.time, night, quality.fogScale, quality.fogSteps, viewDistance);
    }
    const glm::vec4 projectedSun = viewProjection * glm::vec4(lightDirection, 0.0f);
    glm::vec3 sunScreen(0.0f);
    if (!night && !scene.menu && projectedSun.w > 0.0f) {
        sunScreen = glm::vec3(glm::vec2(projectedSun) * (0.5f / projectedSun.w) + 0.5f, 1.0f);
    }
    post_.present(sunScreen, camera.speedEffect, scene.ripple, scene.menu, graphics_.bloom);
}
