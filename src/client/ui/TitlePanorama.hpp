#pragma once
#include <cstdint>
#include <vector>

namespace perlin::ui {
    struct PanoramaVertex {
        float x, y;
        float u, v;
    };

    struct PanoramaFace {
        std::vector<PanoramaVertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct PanoramaView {
        float yawDegrees = 0;
        float pitchDegrees = 0;
        float fovYDegrees = 85;
    };

    PanoramaView PanoramaViewAt(double seconds);

    void BuildPanorama(const PanoramaView& view, int width, int height, PanoramaFace (&faces)[6],
                       int grid = 16);
}
