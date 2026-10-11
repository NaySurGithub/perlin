#include "world/level/lighting/ChunkLighting.hpp"

#include <cstdio>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static ChunkLighting::Grid MakeGrid(int rows) {
    ChunkLighting::Grid grid;
    grid.yBase = 0;
    grid.rows = rows;
    grid.opacity.assign(static_cast<size_t>(rows) * ChunkLighting::kSpan * ChunkLighting::kSpan, 0);
    return grid;
}

int main() {
    {
        ChunkLighting::Grid grid = MakeGrid(8);
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(20, 0, 20)] == 15);
        CHECK(grid.sky[grid.Index(20, 7, 20)] == 15);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int x = 0; x < ChunkLighting::kSpan; x++)
            for (int z = 0; z < ChunkLighting::kSpan; z++)
                grid.opacity[grid.Index(x, 5, z)] = kMaxLight;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(10, 6, 10)] == 15);
        CHECK(grid.sky[grid.Index(10, 5, 10)] == 0);
        CHECK(grid.sky[grid.Index(10, 4, 10)] == 0);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int x = 0; x < ChunkLighting::kSpan; x++)
            for (int z = 0; z < ChunkLighting::kSpan; z++)
                if (x != 24 || z != 24)
                    grid.opacity[grid.Index(x, 5, z)] = kMaxLight;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(24, 4, 24)] == 15);
        CHECK(grid.sky[grid.Index(25, 4, 24)] == 14);
        CHECK(grid.sky[grid.Index(24, 4, 27)] == 12);
        CHECK(grid.sky[grid.Index(27, 4, 27)] == 9);
        CHECK(grid.sky[grid.Index(24, 3, 24)] == 15);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int x = 0; x < ChunkLighting::kSpan; x++)
            for (int z = 0; z < ChunkLighting::kSpan; z++)
                grid.opacity[grid.Index(x, 7, z)] = kMaxLight;
        const uint32_t source = static_cast<uint32_t>(grid.Index(24, 3, 24));
        ChunkLighting::Propagate(grid, {{source, 14}});
        CHECK(grid.block[source] == 14);
        CHECK(grid.block[grid.Index(25, 3, 24)] == 13);
        CHECK(grid.block[grid.Index(24, 3, 27)] == 11);
        CHECK(grid.block[grid.Index(26, 4, 25)] == 10);
        CHECK(grid.block[grid.Index(24, 3, 24 + 14)] == 0);
        CHECK(grid.block[grid.Index(24, 3, 24 + 13)] == 1);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        for (int y = 0; y < 8; y++)
            grid.opacity[grid.Index(25, y, 24)] = kMaxLight;
        const uint32_t source = static_cast<uint32_t>(grid.Index(24, 3, 24));
        ChunkLighting::Propagate(grid, {{source, 15}});
        CHECK(grid.block[grid.Index(25, 3, 24)] == 0);
        CHECK(grid.block[grid.Index(26, 3, 24)] == 11);
    }

    {
        ChunkLighting::Grid grid = MakeGrid(8);
        grid.opacity[grid.Index(24, 5, 24)] = 3;
        ChunkLighting::Propagate(grid, {});
        CHECK(grid.sky[grid.Index(24, 5, 24)] == 12);
        CHECK(grid.sky[grid.Index(24, 4, 24)] == 14);
    }

    std::printf(failures ? "%d FAILED\n" : "light self-tests passed\n", failures);
    return failures ? 1 : 0;
}
