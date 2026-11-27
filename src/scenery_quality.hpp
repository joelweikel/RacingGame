#pragma once

#include <array>

struct SceneryQuality {
    int nearestTreeLod;
    float nearTreePixels;
    float midTreePixels;
    float minTreePixels;
    float propDistance;
    float viewDistance;
    std::array<float, 4> grassDistances; // Dense, medium, sparse, and horizon grass ranges.
    float detailedGrassRadius;
    int grassSamples;
    float fogScale;
    int fogSteps;
    int shadowResolution;
    float shadowExtent;
    float distantShadowExtent;
};

const SceneryQuality& sceneryQuality();
