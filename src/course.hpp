#pragma once

#include <glm/glm.hpp>

class CampWorld;

enum class Course { TestGrid, DustlineOval, DerelictCamp };
enum class GroundSurface { Asphalt, Dirt, Gravel, Sand };

inline constexpr float ovalStraightHalfLength = 46.0f;
inline constexpr float ovalTurnRadius = 26.0f;
inline constexpr float ovalRoadHalfWidth = 5.6f;

struct GroundContact {
    float height;
    GroundSurface surface;
};

GroundContact groundContact(Course course, const CampWorld* camp, const glm::vec3& position, float groundHeight);

constexpr float groundGrip(GroundSurface surface)
{
    switch (surface) {
    case GroundSurface::Dirt: return 0.57f;
    case GroundSurface::Gravel: return 0.72f;
    case GroundSurface::Sand: return 0.52f;
    case GroundSurface::Asphalt: return 1.0f;
    }
    return 1.0f;
}
