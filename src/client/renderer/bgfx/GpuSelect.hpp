#pragma once
#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace perlin::gpu {

    struct Choice {
        bgfx::RendererType::Enum type = bgfx::RendererType::Count;
        uint16_t vendorId = 0;
        uint16_t deviceId = 0;
        std::string adapterName;
        std::string reason;
    };

    Choice ChooseRenderer();

}
