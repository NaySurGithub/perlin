#pragma once
// Vanilla-style title panorama: the six textures/ui/panorama_N images on a cube around a camera
// that turns slowly. Built per frame in screen pixels from a continuous clock, so it moves
// smoothly and at the same speed whatever the frame rate (the old RmlUi strip animation was
// snapped to whole GUI pixels and stepped visibly).
#include <cstdint>
#include <vector>

namespace perlin::ui {
    struct PanoramaVertex {
        float x, y; // screen pixels
        float u, v;
    };

    struct PanoramaFace {
        std::vector<PanoramaVertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct PanoramaView {
        float yawDegrees = 0;   // + turns right
        float pitchDegrees = 0; // + looks down
        float fovYDegrees = 85;
    };

    // Camera for a given time since the title screen first opened: one turn every 240 s, with a
    // gentle pitch sway.
    PanoramaView PanoramaViewAt(double seconds);

    // Fills faces[0..5] (one per panorama texture). Each face is subdivided so the renderer's
    // affine texture interpolation stays visually exact; cells behind the camera are dropped.
    void BuildPanorama(const PanoramaView& view, int width, int height, PanoramaFace (&faces)[6], int grid = 16);
}
