#pragma once
#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace perlin::gpu {

    // The graphics API and adapter Perlin asks bgfx for. type == Count leaves the API choice to bgfx.
    struct Choice {
        bgfx::RendererType::Enum type = bgfx::RendererType::Count;
        uint16_t vendorId = 0; // BGFX_PCI_ID_NONE (0) lets bgfx pick the adapter
        uint16_t deviceId = 0;
        std::string adapterName;
        std::string reason;
    };

    // Windows: probes the DXGI adapters and picks the fastest hardware GPU (the one with the most dedicated memory, so
    // a laptop's discrete GPU wins over its integrated one) and Direct3D 11. Other platforms return the default Choice.
    Choice ChooseRenderer();

}
