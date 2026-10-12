#include "client/renderer/SkyGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        class JavaRandom {
        public:
            explicit JavaRandom(int64_t value) : state((static_cast<uint64_t>(value) ^ 0x5DEECE66DULL) & kMask) {}

            float NextFloat() { return static_cast<float>(Next(24)) / static_cast<float>(1 << 24); }

            double NextDouble() {
                const int64_t high = static_cast<int64_t>(Next(26)) << 27;
                return static_cast<double>(high + Next(27)) * (1.0 / static_cast<double>(1LL << 53));
            }

        private:
            static constexpr uint64_t kMask = (1ULL << 48) - 1;

            int32_t Next(int bits) {
                state = (state * 0x5DEECE66DULL + 0xBULL) & kMask;
                return static_cast<int32_t>(static_cast<int64_t>(state >> (48 - bits)));
            }

            uint64_t state;
        };

        uint32_t Pack(float r, float g, float b, float a) {
            auto q = [](float c) { return static_cast<uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
        }

        void Quad(std::vector<SkyVertex>& out, const float p[4][3], const float uv[4][2], uint32_t color) {
            for (int i = 0; i < 4; i++)
                out.push_back({p[i][0], p[i][1], p[i][2], color, uv[i][0], uv[i][1]});
        }
    }

    std::vector<SkyVertex> BuildStars() {
        std::vector<SkyVertex> out;
        JavaRandom random(10842);
        const uint32_t white = Pack(1, 1, 1, 1);
        for (int i = 0; i < 1500; i++) {
            double x = random.NextFloat() * 2.0f - 1.0f;
            double y = random.NextFloat() * 2.0f - 1.0f;
            double z = random.NextFloat() * 2.0f - 1.0f;
            const double size = 0.15f + random.NextFloat() * 0.1f;
            double length = x * x + y * y + z * z;
            if (length >= 1.0 || length <= 0.01)
                continue;
            length = 1.0 / std::sqrt(length);
            x *= length;
            y *= length;
            z *= length;
            const double px = x * 100.0, py = y * 100.0, pz = z * 100.0;
            const double yaw = std::atan2(x, z);
            const double sy = std::sin(yaw), cy = std::cos(yaw);
            const double pitch = std::atan2(std::sqrt(x * x + z * z), y);
            const double sp = std::sin(pitch), cp = std::cos(pitch);
            const double spin = random.NextDouble() * 3.141592653589793 * 2.0;
            const double ss = std::sin(spin), cs = std::cos(spin);
            for (int j = 0; j < 4; j++) {
                const double a = ((j & 2) - 1) * size;
                const double b = (((j + 1) & 2) - 1) * size;
                const double rx = a * cs - b * ss;
                const double rz = b * cs + a * ss;
                const double ty = rx * sp;
                const double tz = -rx * cp;
                const double fx = tz * sy - rz * cy;
                const double fz = rz * sy + tz * cy;
                out.push_back({static_cast<float>(px + fx), static_cast<float>(py + ty), static_cast<float>(pz + fz),
                               white, 0.0f, 0.0f});
            }
        }
        return out;
    }

    std::vector<SkyVertex> BuildClouds(bool fancy) {
        std::vector<SkyVertex> out;
        constexpr float kTexel = 1.0f / kCloudTexture;
        constexpr float kEpsilon = 9.765625e-4f;
        constexpr float kHeight = 4.0f;
        const float shade[3] = {0.7f, 0.9f, 0.8f};
        const uint32_t bottom = Pack(shade[0], shade[0], shade[0], 0.8f);
        const uint32_t top = Pack(1, 1, 1, 0.8f);
        const uint32_t sideX = Pack(shade[1], shade[1], shade[1], 0.8f);
        const uint32_t sideZ = Pack(shade[2], shade[2], shade[2], 0.8f);

        for (int tx = -3; tx <= 4; tx++)
            for (int tz = -3; tz <= 4; tz++) {
                const float cx = static_cast<float>(tx * 8), cz = static_cast<float>(tz * 8);
                if (!fancy) {
                    const float p[4][3] = {{cx, 0, cz}, {cx + 8, 0, cz}, {cx + 8, 0, cz + 8}, {cx, 0, cz + 8}};
                    const float uv[4][2] = {{cx * kTexel, cz * kTexel},
                                            {(cx + 8) * kTexel, cz * kTexel},
                                            {(cx + 8) * kTexel, (cz + 8) * kTexel},
                                            {cx * kTexel, (cz + 8) * kTexel}};
                    Quad(out, p, uv, top);
                    continue;
                }
                {
                    const float p[4][3] = {{cx, 0, cz + 8}, {cx + 8, 0, cz + 8}, {cx + 8, 0, cz}, {cx, 0, cz}};
                    const float uv[4][2] = {{cx * kTexel, (cz + 8) * kTexel},
                                            {(cx + 8) * kTexel, (cz + 8) * kTexel},
                                            {(cx + 8) * kTexel, cz * kTexel},
                                            {cx * kTexel, cz * kTexel}};
                    Quad(out, p, uv, bottom);
                }
                {
                    const float y = kHeight - kEpsilon;
                    const float p[4][3] = {{cx, y, cz}, {cx + 8, y, cz}, {cx + 8, y, cz + 8}, {cx, y, cz + 8}};
                    const float uv[4][2] = {{cx * kTexel, cz * kTexel},
                                            {(cx + 8) * kTexel, cz * kTexel},
                                            {(cx + 8) * kTexel, (cz + 8) * kTexel},
                                            {cx * kTexel, (cz + 8) * kTexel}};
                    Quad(out, p, uv, top);
                }
                for (int slice = 0; slice < 8; slice++) {
                    const float u = (cx + slice + 0.5f) * kTexel;
                    const float v0 = cz * kTexel, v1 = (cz + 8) * kTexel;
                    if (tx > -1) {
                        const float x = cx + slice;
                        const float p[4][3] = {{x, 0, cz + 8}, {x, kHeight, cz + 8}, {x, kHeight, cz}, {x, 0, cz}};
                        const float uv[4][2] = {{u, v1}, {u, v1}, {u, v0}, {u, v0}};
                        Quad(out, p, uv, sideX);
                    }
                    if (tx <= 1) {
                        const float x = cx + slice + 1 - kEpsilon;
                        const float p[4][3] = {{x, 0, cz + 8}, {x, kHeight, cz + 8}, {x, kHeight, cz}, {x, 0, cz}};
                        const float uv[4][2] = {{u, v1}, {u, v1}, {u, v0}, {u, v0}};
                        Quad(out, p, uv, sideX);
                    }
                }
                for (int slice = 0; slice < 8; slice++) {
                    const float v = (cz + slice + 0.5f) * kTexel;
                    const float u0 = cx * kTexel, u1 = (cx + 8) * kTexel;
                    if (tz > -1) {
                        const float z = cz + slice;
                        const float p[4][3] = {{cx, kHeight, z}, {cx + 8, kHeight, z}, {cx + 8, 0, z}, {cx, 0, z}};
                        const float uv[4][2] = {{u0, v}, {u1, v}, {u1, v}, {u0, v}};
                        Quad(out, p, uv, sideZ);
                    }
                    if (tz <= 1) {
                        const float z = cz + slice + 1 - kEpsilon;
                        const float p[4][3] = {{cx, kHeight, z}, {cx + 8, kHeight, z}, {cx + 8, 0, z}, {cx, 0, z}};
                        const float uv[4][2] = {{u0, v}, {u1, v}, {u1, v}, {u0, v}};
                        Quad(out, p, uv, sideZ);
                    }
                }
            }
        return out;
    }

    CloudParams ComputeClouds(float eyeX, float eyeY, float eyeZ, float cloudTicks, float celestialAngle) {
        CloudParams out;
        double x = (eyeX + cloudTicks * 0.03) / 12.0;
        double z = eyeZ / 12.0 + 0.33;
        x -= std::floor(x / 2048.0) * 2048.0;
        z -= std::floor(z / 2048.0) * 2048.0;
        out.offsetX = static_cast<float>(x - std::floor(x));
        out.offsetZ = static_cast<float>(z - std::floor(z));
        out.uvX = static_cast<float>(std::floor(x)) / kCloudTexture;
        out.uvZ = static_cast<float>(std::floor(z)) / kCloudTexture;
        out.height = kCloudLevel - eyeY + 0.33f;

        const float brightness = std::clamp(std::cos(celestialAngle * 6.2831853f) * 2.0f + 0.5f, 0.0f, 1.0f);
        out.color[0] = out.color[1] = brightness * 0.9f + 0.1f;
        out.color[2] = brightness * 0.85f + 0.15f;
        return out;
    }

    void SkyRotation(float celestialAngle, float out[16]) {
        const float a = celestialAngle * 6.2831853f;
        const float c = std::cos(a), s = std::sin(a);
        out[0] = 0;  out[1] = 0;  out[2] = 1;  out[3] = 0;
        out[4] = -s; out[5] = c;  out[6] = 0;  out[7] = 0;
        out[8] = -c; out[9] = -s; out[10] = 0; out[11] = 0;
        out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 1;
    }

    int MoonPhase(int64_t day) {
        return static_cast<int>(((day % 8) + 8) % 8);
    }
}
