#pragma once
#include "client/ui/HudState.hpp"

#include <array>
#include <string>

namespace perlin::ui {
    struct InventoryView {
        std::array<HudItem, 36> slots;
        std::array<HudItem, 4> armor;
        std::array<HudItem, 4> craft;
        HudItem offhand, output, cursor;
        int selectedHotbar = 0;
        int hovered = -1;
        float mouseX = 0, mouseY = 0;
        std::string title;
    };

    inline constexpr int kInvPanelW = 176;
    inline constexpr int kInvPanelH = 166;
    inline constexpr int kInvAddrArmor = 36, kInvAddrOffhand = 40, kInvAddrCraft = 41, kInvAddrOutput = 45;
    void InventorySlotPos(int addr, int& x, int& y);

    std::string DefaultInventoryScreenTemplate();
    std::string BuildInventoryScreenRml(const std::string& templateText, const InventoryView& view, int guiW, int guiH);
    std::string PrettyItemName(const std::string& id);
    int InventoryAddrFromId(const std::string& id);
}
