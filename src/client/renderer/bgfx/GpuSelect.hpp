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

    // Windows: probes the DXGI adapters and picks the fastest hardware GPU. Direct3D 12 is chosen when that GPU
    // runs it properly (feature level 12_0, 12_1 on Intel graphics, a real adapter and not a remote session),
    // Direct3D 11 otherwise. Other platforms return the default Choice.
    Choice ChooseRenderer();

    // A Direct3D 12 failure at run time (device removed, creation failed) is remembered so the next start uses
    // Direct3D 11 instead of crashing in a loop. Clearing is by deleting d3d12_disabled in the data directory.
    void RememberD3D12Failure(const std::string& why);
    bool D3D12Disabled();

}
