#pragma once
#include "client/ui/JsonScreen.hpp"

#include <string>
#include <vector>

// Turns Perlin's own state into the bindings Bedrock's screen controllers would publish, one function per screen.
// Only bindings the vanilla files (or common server overrides) read are answered; the rest keep their defaults.
namespace perlin::ui {
    struct PausePlayer {
        std::string name;
        int permission = 1;  // 0 visitor, 1 member, 2 operator
        bool local = false;
    };

    struct PauseModel {
        std::string playerName;
        std::string listTitle;
        std::vector<PausePlayer> players;
        bool multiplayer = false;
    };
    ScreenData PauseScreenData(const PauseModel& model);

    // Slot bindings for container screens. `container` is the open container's items; `inventory` the player's 36
    // slots (hotbar first). Item pictures come from JsonScreens::SetItems.
    ScreenData ContainerScreenData(const std::vector<HudItem>& container, const std::vector<HudItem>& inventory, int selectedHotbar);
}
