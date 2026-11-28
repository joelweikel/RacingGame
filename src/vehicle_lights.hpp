#pragma once

#include "graphics.hpp"

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

struct VehicleLights {
    std::array<glm::vec4, 4> lamps{};
    glm::vec3 direction{1.0f, 0.0f, 0.0f};
};

class VehicleLightUniforms {
public:
    void initialize(GLuint program)
    {
        lamps_ = glGetUniformLocation(program, "uVehicleLamps[0]");
        beam_ = glGetUniformLocation(program, "uVehicleForward");
    }

    void bind(const VehicleLights& lights) const
    {
        glUniform4fv(lamps_, static_cast<GLsizei>(lights.lamps.size()), glm::value_ptr(lights.lamps.front()));
        glUniform3fv(beam_, 1, glm::value_ptr(lights.direction));
    }

private:
    GLint lamps_ = -1;
    GLint beam_ = -1;
};

inline constexpr const char* vehicleLightingGlsl = R"glsl(
uniform vec4 uVehicleLamps[4];
uniform vec3 uVehicleForward;

vec3 vehicleLighting(vec3 point, vec3 normal) {
    if (uVehicleLamps[0].w + uVehicleLamps[2].w <= 0.0) return vec3(0.0);
    vec3 color = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec4 lamp = uVehicleLamps[i];
        if (lamp.w <= 0.0) continue;
        bool headlight = i < 2;
        vec3 ray = point - lamp.xyz;
        float rangeSquared = dot(ray, ray);
        if (rangeSquared > (headlight ? 2304.0 : 81.0)) continue;
        float range = sqrt(max(rangeSquared, 0.01));
        vec3 toSurface = ray / range;
        vec3 beam = headlight ? uVehicleForward : -uVehicleForward;
        float angle = dot(beam, toSurface);
        float facing = max(0.0, dot(normal, -toSurface));
        if (headlight) {
            float cone = smoothstep(0.87, 0.985, angle);
            float cutoff = 1.0 - smoothstep(-0.014, 0.060, toSurface.y - beam.y);
            float reach = 1.0 - smoothstep(25.0, 48.0, range);
            float intensity = 7.4 * cone * cutoff * reach * (0.17 + facing * 0.83) /
                              (1.0 + rangeSquared * 0.022);
            color += vec3(1.0, 0.90, 0.73) * intensity * lamp.w;
        } else {
            float cone = smoothstep(0.22, 0.83, angle);
            float reach = 1.0 - smoothstep(4.0, 9.0, range);
            float intensity = 3.4 * cone * reach * (0.22 + facing * 0.78) /
                              (1.0 + rangeSquared * 0.46);
            color += vec3(1.0, 0.025, 0.012) * intensity * lamp.w;
        }
    }
    return color;
}
)glsl";
