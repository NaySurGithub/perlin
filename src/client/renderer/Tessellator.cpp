#include "client/renderer/Tessellator.hpp"

namespace perlin {
    void Tessellator::Begin() {
        vertices.clear();
        for (auto& list : indices)
            list.clear();
        sections.clear();
        layer = RenderLayer::Solid;
        building = true;
    }

    void Tessellator::Reserve(size_t quads) {
        vertices.reserve(quads * 4);
        indices[0].reserve(quads * 6);
    }

    void Tessellator::AddQuad(const float v[4][3], const float c[4][3], const float uv[4][2], bool flip,
                              const float light[4][2]) {
        if (!building)
            return;
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < 4; i++)
            vertices.push_back({v[i][0], v[i][1], v[i][2], PackColor(c[i][0], c[i][1], c[i][2]), uv[i][0],
                                uv[i][1], PackLight(light[i][0], light[i][1])});

        std::vector<uint32_t>& out = indices[static_cast<size_t>(layer)];
        static constexpr uint32_t kForward[6] = {0, 1, 2, 0, 2, 3};
        static constexpr uint32_t kFlipped[6] = {1, 2, 3, 1, 3, 0};
        const uint32_t* pattern = flip ? kFlipped : kForward;
        for (int i = 0; i < 6; i++)
            out.push_back(base + pattern[i]);
    }

    void Tessellator::AddQuad(const float v[4][3], const float c[4][3], const float uv[4][2], bool flip,
                              float block, float sky) {
        const float light[4][2] = {{block, sky}, {block, sky}, {block, sky}, {block, sky}};
        AddQuad(v, c, uv, flip, light);
    }

    void Tessellator::EnterSection(int sy) {
        if (!building)
            return;
        if (!sections.empty() && sections.back().sy == sy)
            return;
        MeshSection section{};
        section.sy = sy;
        for (int i = 0; i < kRenderLayerCount; i++)
            section.start[i] = static_cast<uint32_t>(indices[i].size());
        sections.push_back(section);
    }

    void Tessellator::End() {
        if (!building)
            return;
        building = false;

        const size_t n = sections.size();
        for (size_t i = 0; i < n; i++)
            for (int l = 0; l < kRenderLayerCount; l++) {
                const uint32_t end =
                    i + 1 < n ? sections[i + 1].start[l] : static_cast<uint32_t>(indices[l].size());
                sections[i].count[l] = end - sections[i].start[l];
            }
        std::erase_if(sections,
                      [](const MeshSection& s) { return !s.count[0] && !s.count[1] && !s.count[2]; });
    }
}
