#pragma once

inline constexpr const char* shadowSamplingGlsl = R"glsl(
uniform mat4 uLightMatrix;
uniform mat4 uDistantLightMatrix;
uniform sampler2D uShadowMap;
uniform sampler2D uDistantShadowMap;
uniform bool uHasDistantShadow;

float shadowCoverage(vec3 uv) {
    if (uv.z < 0.0 || uv.z > 1.0) return 0.0;
    float edge = max(abs(uv.x - 0.5), abs(uv.y - 0.5));
    return 1.0 - smoothstep(uHasDistantShadow ? 0.42 : 0.46, 0.49, edge);
}

float shadowTaps(sampler2D map, vec3 uv, float bias, int kernel) {
    float depth = uv.z - bias;
    if (kernel == 0) return step(depth, texture(map, uv.xy).r);
    vec2 texel = 1.25 / vec2(textureSize(map, 0));
    if (kernel == 1) {
        return 0.5 * (step(depth, texture(map, uv.xy - texel).r) +
                      step(depth, texture(map, uv.xy + texel).r));
    }
    int radius = kernel - 1;
    float lit = 0.0;
    float total = 0.0;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            float weight = radius == 1 ? 1.0 : float((3 - abs(x)) * (3 - abs(y)));
            lit += weight * step(depth, texture(map, uv.xy + vec2(x, y) * texel).r);
            total += weight;
        }
    }
    return lit / total;
}

float sceneShadow(vec3 point, float bias, int kernel) {
    vec3 uv = (uLightMatrix * vec4(point, 1.0)).xyz * 0.5 + 0.5;
    float nearCoverage = shadowCoverage(uv);
    float nearLight = 1.0;
    if (nearCoverage > 0.0) nearLight = shadowTaps(uShadowMap, uv, bias, kernel);
    if (nearCoverage >= 1.0) return nearLight;

    float distantLight = 1.0;
    if (uHasDistantShadow) {
        vec3 distant = (uDistantLightMatrix * vec4(point, 1.0)).xyz * 0.5 + 0.5;
        float coverage = shadowCoverage(distant);
        if (coverage > 0.0) {
            distantLight = mix(1.0, shadowTaps(uDistantShadowMap, distant, bias * 0.42, min(kernel, 2)), coverage);
        }
    }
    return mix(distantLight, nearLight, nearCoverage);
}
)glsl";
