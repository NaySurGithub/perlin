#include "client/entity/BlockEntityView.hpp"

#include "world/level/block/BlockPalette.hpp"

#include <cstdlib>
#include <unordered_set>

namespace perlin::entity {
    namespace {
        float BedYaw(int direction) {
            switch (direction & 3) {
                case 0: return 0.0f;
                case 1: return 90.0f;
                case 2: return 180.0f;
                default: return 270.0f;
            }
        }

        void BedStep(int direction, int& dx, int& dz) {
            switch (direction & 3) {
                case 0: dx = 0; dz = 1; break;
                case 1: dx = -1; dz = 0; break;
                case 2: dx = 0; dz = -1; break;
                default: dx = 1; dz = 0; break;
            }
        }

        float FacingYaw(int facing) {
            switch (facing) {
                case 2: return 180.0f;
                case 4: return 90.0f;
                case 5: return 270.0f;
                default: return 0.0f;
            }
        }

        int FacingToGround(int facing) {
            switch (facing) {
                case 2: return 8;
                case 4: return 4;
                case 5: return 12;
                default: return 0;
            }
        }
    }

    void BuildBlockEntityDraws(const std::vector<SpecialBlock>& specials, const BlockEntityStore& store, std::vector<BlockEntityDraw>& out) {
        BlockPalette& palette = BlockPalette::Instance();
        std::unordered_set<uint64_t> beds;
        const char* forced = std::getenv("PERLIN_PREVIEW_SIGNTEXT");
        for (const SpecialBlock& s : specials) {
            const std::string name = palette.Name(s.id);
            const std::string shortName = name.rfind("minecraft:", 0) == 0 ? name.substr(10) : name;
            BlockEntity be;
            if (shortName == "bed") {
                const int direction = palette.StateInt(s.id, "direction", 0);
                const bool head = palette.StateInt(s.id, "head_piece_bit", 0) != 0;
                int dx = 0, dz = 0;
                BedStep(direction, dx, dz);
                const int fx = head ? s.x - dx : s.x, fz = head ? s.z - dz : s.z;
                const int ox = head ? fx : fx + dx, oz = head ? fz : fz + dz;
                const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(fx)) << 40) ^ (static_cast<uint64_t>(static_cast<uint32_t>(fz)) << 8) ^
                                     static_cast<uint64_t>(s.y + 4096);
                if (!beds.insert(key).second)
                    continue;
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Bed;
                d.origin = {static_cast<double>(fx), static_cast<double>(s.y), static_cast<double>(fz)};
                d.yaw = BedYaw(direction);
                int color = -1;
                if (store.Get(s.x, s.y, s.z, be))
                    color = be.color;
                if (color < 0 && store.Get(ox, s.y, oz, be))
                    color = be.color;
                d.color = color < 0 ? 14 : color;
                out.push_back(std::move(d));
                continue;
            }
            BlockEntityDraw d;
            d.kind = BlockEntityDraw::Kind::Sign;
            d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
            const bool wall = shortName.find("wall_sign") != std::string::npos;
            const bool hangingSign = shortName.find("hanging_sign") != std::string::npos;
            if (!store.Get(s.x, s.y, s.z, be) && !forced)
                continue;
            if (forced && be.front.text.empty())
                be.front.text = forced;
            if (be.front.text.empty() && be.back.text.empty())
                continue;
            d.front = be.front.text;
            d.back = be.back.text;
            d.frontColor = be.front.color;
            d.backColor = be.back.color;
            d.frontGlow = be.front.glow;
            d.backGlow = be.back.glow;
            const int facing = palette.StateInt(s.id, "facing_direction", 2);
            if (wall) {
                d.wall = true;
                d.yaw = FacingYaw(facing);
            } else if (hangingSign) {
                d.hanging = true;
                const bool hangs = palette.StateInt(s.id, "hanging", 0) != 0;
                if (hangs) {
                    const bool attached = palette.StateInt(s.id, "attached_bit", 0) != 0;
                    const int k = attached ? palette.StateInt(s.id, "ground_sign_direction", 0) % 16 : FacingToGround(facing);
                    d.yaw = static_cast<float>(k) * 22.5f;
                } else
                    d.yaw = FacingYaw(facing);
            } else {
                d.yaw = static_cast<float>(palette.StateInt(s.id, "ground_sign_direction", 0) % 16) * 22.5f;
            }
            out.push_back(std::move(d));
        }
    }
}
