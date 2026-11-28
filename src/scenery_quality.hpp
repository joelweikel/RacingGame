#pragma once

#include <array>

enum class QualityLevel { Automatic, Low, Medium, High };

struct GraphicsSettings {
    QualityLevel preset = QualityLevel::Automatic;
    QualityLevel lod = QualityLevel::Automatic;
    int renderScale = 2;
    bool bloom = true;
    bool reflections = true;

    float resolutionScale() const { return std::array{0.67f, 0.85f, 1.0f}[renderScale]; }
};

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

SceneryQuality sceneryQuality(QualityLevel preset = QualityLevel::Automatic,
                              QualityLevel lod = QualityLevel::Automatic);
