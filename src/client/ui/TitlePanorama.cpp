#include "client/ui/TitlePanorama.hpp"

#include <cmath>

namespace perlin::ui {
    namespace {
        struct V3 {
            float x, y, z;
        };

        V3 Mix(const V3& a, const V3& b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
        float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

        constexpr V3 kFace[6][4] = {
            {{-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}},
            {{1, 1, -1}, {1, 1, 1}, {1, -1, 1}, {1, -1, -1}},
            {{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}},
            {{-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}},
            {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
            {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
        };
    }

    PanoramaView PanoramaViewAt(double seconds) {
        PanoramaView v;
        v.yawDegrees = static_cast<float>(std::fmod(seconds * (360.0 / 240.0), 360.0));
        v.pitchDegrees = static_cast<float>(8.0 + 3.0 * std::sin(seconds * 0.05));
        return v;
    }

    void BuildPanorama(const PanoramaView& view, int width, int height, PanoramaFace (&faces)[6], int grid) {
        constexpr float kDeg = 3.14159265358979f / 180.0f;
        const float yaw = view.yawDegrees * kDeg, pitch = view.pitchDegrees * kDeg;
        const V3 forward{std::cos(pitch) * std::sin(yaw), -std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
        const V3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
        const V3 up{std::sin(pitch) * std::sin(yaw), std::cos(pitch), -std::sin(pitch) * std::cos(yaw)};
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float ty = std::tan(view.fovYDegrees * 0.5f * kDeg), tx = ty * aspect;
        constexpr float kNear = 0.02f;
        const int n = grid < 1 ? 1 : grid;

        for (int f = 0; f < 6; f++) {
            PanoramaFace& out = faces[f];
            out.vertices.clear();
            out.indices.clear();
            const V3* c = kFace[f];
            std::vector<PanoramaVertex> pts(static_cast<size_t>((n + 1) * (n + 1)));
            std::vector<char> ok(pts.size());
            for (int j = 0; j <= n; j++) {
                const float v = static_cast<float>(j) / static_cast<float>(n);
                const V3 left = Mix(c[0], c[3], v), rightEdge = Mix(c[1], c[2], v);
                for (int i = 0; i <= n; i++) {
                    const float u = static_cast<float>(i) / static_cast<float>(n);
                    const V3 p = Mix(left, rightEdge, u);
                    const float depth = Dot(p, forward);
                    const size_t k = static_cast<size_t>(j * (n + 1) + i);
                    ok[k] = depth > kNear;
                    if (!ok[k])
                        continue;
                    const float nx = Dot(p, right) / (depth * tx), ny = Dot(p, up) / (depth * ty);
                    pts[k] = {(nx + 1.0f) * 0.5f * static_cast<float>(width), (1.0f - ny) * 0.5f * static_cast<float>(height), u, v};
                }
            }
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    const size_t a = static_cast<size_t>(j * (n + 1) + i), b = a + 1, d = a + static_cast<size_t>(n + 1), e = d + 1;
                    if (!ok[a] || !ok[b] || !ok[d] || !ok[e])
                        continue;
                    const float minX = std::fmin(std::fmin(pts[a].x, pts[b].x), std::fmin(pts[d].x, pts[e].x));
                    const float maxX = std::fmax(std::fmax(pts[a].x, pts[b].x), std::fmax(pts[d].x, pts[e].x));
                    const float minY = std::fmin(std::fmin(pts[a].y, pts[b].y), std::fmin(pts[d].y, pts[e].y));
                    const float maxY = std::fmax(std::fmax(pts[a].y, pts[b].y), std::fmax(pts[d].y, pts[e].y));
                    if (maxX < 0 || maxY < 0 || minX > static_cast<float>(width) || minY > static_cast<float>(height))
                        continue;
                    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
                    out.vertices.push_back(pts[a]);
                    out.vertices.push_back(pts[b]);
                    out.vertices.push_back(pts[e]);
                    out.vertices.push_back(pts[d]);
                    const uint32_t q[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
                    out.indices.insert(out.indices.end(), q, q + 6);
                }
        }
    }
}
