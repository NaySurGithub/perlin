#pragma once
#include <string>

namespace perlin::ui {
    inline constexpr const char* kBedLeaveId = "leave";
    // The in-bed overlay (a single "Leave Bed" button above the hotbar), written entirely as RML.
    std::string BuildBedScreenRml(const std::string& label, int guiW, int guiH);
}
